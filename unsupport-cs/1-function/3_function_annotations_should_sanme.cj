// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// SEPARATE-FILE: c/c.cj
// EXEC-0: %compiler --dump-ast %noColor --experimental c/c.cj --output-type=chir --output-dir ./c
package cmp
@Annotation 
 public class BB{
    const init(){}
}

@Annotation
common public class A{
    const init(){}
}
@A[a:1+2]
common  func foo(input!:String = "common"): Unit{
    println("From common member foo func")
    println(input)
}
    

// SEPARATE-FILE: p/p.cj
// EXEC-0: %compiler --dump-ast %noColor --experimental p/p.cj c/cmp.chir --common-part-cjo=./c/cmp.cjo -o %output
// (LLVM and Linux)RUN-EXEC-PIPE-0: LD_LIBRARY_PATH=$LD_LIBRARY_PATH:./p && %run %output 2>&1 | compare %f
// (LLVM and Windows)RUN-EXEC-PIPE-0: set PATH=./p;%PATH% && %run %output 2>&1 | compare %f
// (LLVM and Darwin)RUN-EXEC-PIPE-0: DYLD_LIBRARY_PATH=$DYLD_LIBRARY_PATH:./p && %run %output 2>&1 | compare %f
package cmp

@Annotation
platform public class A{}
   
@A[a:1+3]
platform  func foo(input!:String ): Unit {
    println("From platform member foo func")
    println(input)
}
main() {
    foo(input:"platform")
}

/* SCAN
From platform member foo func
platform
*/