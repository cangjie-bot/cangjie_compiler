// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// SEPARATE-FILE: c/c.cj
// EXEC-0: %compiler %noColor --experimental c/c.cj --output-type=chir --output-dir ./c
package cmp


open public class A{
    const init(){}
}

class B<:A{}
common  func foo(input!:String = "common"): A{
    println("From common member foo func")
    println(input)
    A()
}
    

// SEPARATE-FILE: p/p.cj
// EXEC-0: %compiler --dump-chir %noColor --experimental p/p.cj c/cmp.chir --common-part-cjo=./c/cmp.cjo -o %output
// (LLVM and Linux)RUN-EXEC-PIPE-0: LD_LIBRARY_PATH=$LD_LIBRARY_PATH:./p && %run %output 2>&1 | compare %f
// (LLVM and Windows)RUN-EXEC-PIPE-0: set PATH=./p;%PATH% && %run %output 2>&1 | compare %f
// (LLVM and Darwin)RUN-EXEC-PIPE-0: DYLD_LIBRARY_PATH=$DYLD_LIBRARY_PATH:./p && %run %output 2>&1 | compare %f
package cmp

platform  func foo(input!:String ): B {
    println("From platform member foo func")
    B()
}
main() {

    var x:B= foo() //A
    // foo(input:"platform")
    0
}

/* SCAN
From platform member foo func
platform
*/