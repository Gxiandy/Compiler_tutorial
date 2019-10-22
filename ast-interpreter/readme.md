### 0x01 AST-interpreter
编译原理课程第一次大作业，一个基于clang AST的类C语言解释器的简单实现。  
* 类型支持：int, char , int[], char[], int*  
* 运算符支持：单目（+,-,*） 双目(比较运算,赋值,四则运算)  
* 控制语句支持：Call, return, for, while, if  
* 支持全局变量

### 0x02 运行环境

llvm-5.0.0  
clang-5.0.0

### 0x03 编译运行
1. 将ast-interpreter移动到llvm_root_dir/tools/clang/tools/
2. 修改llvm_root_dir/tools/clang/tools/CMakeLists.txt,在末尾添加add_clang_subdirectory(ast-interpreter)
3. mkdir /llvm_root_dir/build
4. cd /llvm_root/dir/build
5. cmake -G "Unix Makefiles"
6. make
7. 编译好的ast-interpreter将位于llvm_root_dir/build/bin/中
8. ``ast-interpreter " `cat testXX.c`" ``运行解释程序





