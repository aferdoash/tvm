/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/*!
 * \file dso_dll_module.cc
 * \brief Module to load from dynamic shared library.
 */
#include <tvm/runtime/module.h>
#include <tvm/runtime/memory.h>
#include <tvm/runtime/registry.h>
#include <tvm/runtime/packed_func.h>
#include "module_util.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace tvm {
namespace runtime {

// Module to load from dynamic shared libary.
// This is the default module TVM used for host-side AOT
class DSOModuleNode final : public ModuleNode {
 public:
  ~DSOModuleNode() {
    if (lib_handle_) Unload();
  }

  const char* type_key() const final {
    return "dso";
  }

  PackedFunc GetFunction(
      const std::string& name,
      const ObjectPtr<Object>& sptr_to_self) final {
    BackendPackedCFunc faddr;
    if (name == runtime::symbol::tvm_module_main) {
      const char* entry_name = reinterpret_cast<const char*>(
          GetSymbol(runtime::symbol::tvm_module_main));
      CHECK(entry_name!= nullptr)
          << "Symbol " << runtime::symbol::tvm_module_main << " is not presented";
      faddr = reinterpret_cast<BackendPackedCFunc>(GetSymbol(entry_name));
    } else {
      faddr = reinterpret_cast<BackendPackedCFunc>(GetSymbol(name.c_str()));
    }
    if (faddr == nullptr) return PackedFunc();
    return WrapPackedFunc(faddr, sptr_to_self);
  }

  void Init(const std::string& name) {
    Load(name);
    if (auto *ctx_addr =
        reinterpret_cast<void**>(GetSymbol(runtime::symbol::tvm_module_ctx))) {
      *ctx_addr = this;
    }
    InitContextFunctions([this](const char* fname) {
        return GetSymbol(fname);
      });
    // Load the imported modules
    const char* dev_mblob =
        reinterpret_cast<const char*>(
            GetSymbol(runtime::symbol::tvm_dev_mblob));
    if (dev_mblob != nullptr) {
      ImportModuleBlob(dev_mblob, &imports_);
    }
  }

 private:
  // Platform dependent handling.
#if defined(_WIN32)
  // library handle
  HMODULE lib_handle_{nullptr};
  // Load the library
  void Load(const std::string& name) {
    // use wstring version that is needed by LLVM.
    std::wstring wname(name.begin(), name.end());
    lib_handle_ = LoadLibraryW(wname.c_str());
    CHECK(lib_handle_ != nullptr)
        << "Failed to load dynamic shared library " << name;
  }
  void* GetSymbol(const char* name) {
    return reinterpret_cast<void*>(
        GetProcAddress(lib_handle_, (LPCSTR)name)); // NOLINT(*)
  }
  void Unload() {
    FreeLibrary(lib_handle_);
  }
#else
  // Library handle
  void* lib_handle_{nullptr};
  // load the library
  void Load(const std::string& name) {
    lib_handle_ = dlopen(name.c_str(), RTLD_LAZY | RTLD_LOCAL);
    CHECK(lib_handle_ != nullptr)
        << "Failed to load dynamic shared library " << name
        << " " << dlerror();
  }
  void* GetSymbol(const char* name) {
    return dlsym(lib_handle_, name);
  }
  void Unload() {
    dlclose(lib_handle_);
  }
#endif
};

TVM_REGISTER_GLOBAL("module.loadfile_so")
.set_body([](TVMArgs args, TVMRetValue* rv) {
    auto n = make_object<DSOModuleNode>();
    n->Init(args[0]);
    *rv = runtime::Module(n);
  });
}  // namespace runtime
}  // namespace tvm
