/*
 * This file is part of hipSYCL, a SYCL implementation based on CUDA/HIP
 *
 * Copyright (c) 2023 Aksel Alpay and contributors
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */




#include <llvm/IR/GlobalValue.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <string>
#include "hipSYCL/compiler/reflection/FunctionNameExtractionPass.hpp"
#include <llvm/IR/PassManager.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/Support/Alignment.h>

namespace hipsycl {
namespace compiler {

llvm::PreservedAnalyses FunctionNameExtractionPass::run(llvm::Module &M, llvm::ModuleAnalysisManager &AM) {
  std::string GVPrefix = "__acpp_functioninfo_";

  llvm::SmallVector<std::pair<llvm::Function*, llvm::GlobalVariable*>> FunctionsToProcess;
  for(auto& F : M) {
    if(F.hasAddressTaken()) {
      

      auto FunctionName = F.getName().str();
      llvm::Constant *Initializer = llvm::ConstantDataArray::getRaw(
          FunctionName + '\0', FunctionName.size() + 1, llvm::Type::getInt8Ty(M.getContext()));
      
      llvm::GlobalVariable *NameGV = new llvm::GlobalVariable(M, Initializer->getType(), true,
                                                                llvm::GlobalValue::InternalLinkage,
                                                                Initializer, GVPrefix+FunctionName);
      NameGV->setAlignment(llvm::Align{1});
      FunctionsToProcess.push_back(std::make_pair(&F, NameGV));
    }
  }

  llvm::Function* MapFunc = nullptr;
  for(auto& F : M)
    if(F.getName().contains("__acpp_reflection_associate_function_pointer"))
      MapFunc = &F;

  if(MapFunc) {
    if(auto* InitFunc = M.getFunction("__acpp_reflection_init_registered_function_pointers")){
      if(InitFunc->isDeclaration() && (MapFunc->getFunctionType()->getNumParams() == 2)) {
        InitFunc->setLinkage(llvm::GlobalValue::InternalLinkage);

        llvm::BasicBlock *BB =
          llvm::BasicBlock::Create(M.getContext(), "", InitFunc);

        for(const auto& FuncGVPair : FunctionsToProcess) {
          llvm::SmallVector<llvm::Value*> Args;
          Args.push_back(llvm::BitCastInst::Create(llvm::Instruction::BitCast, FuncGVPair.first,
                                                   MapFunc->getFunctionType()->getParamType(0), "",
                                                   BB));
          
          auto Zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(M.getContext()), 0);
          llvm::SmallVector<llvm::Value*> GEPIndices{Zero, Zero};
          auto *GVGEPInst = llvm::GetElementPtrInst::CreateInBounds(
              FuncGVPair.second->getValueType(), FuncGVPair.second,
              llvm::ArrayRef<llvm::Value *>{GEPIndices}, "", BB);
          Args.push_back(GVGEPInst);
              
          llvm::CallInst::Create(llvm::FunctionCallee(MapFunc), llvm::ArrayRef<llvm::Value *>{Args},
                                 "", BB);
        }

        llvm::ReturnInst::Create(M.getContext(), BB);
      }
    }
  }

  return llvm::PreservedAnalyses::none();
}


}
}

