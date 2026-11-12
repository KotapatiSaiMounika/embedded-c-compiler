# Member 2 – Semantic Analysis, Symbol Table, IR Generation and Optimization

## 1. Overview

Member 2 is responsible for implementing the middle-end components of the Embedded C Subset Compiler. These components analyze the Abstract Syntax Tree (AST), verify the semantic correctness of the source program, maintain a symbol table, generate Intermediate Representation (IR), and optimize the generated IR.

The main objective is to transform the parsed source program into an optimized intermediate representation that can be used by the code generation module.

## 2. Responsibilities

The responsibilities of Member 2 include:

- Implementing Semantic Analysis.
- Developing and maintaining the Symbol Table.
- Checking variable declarations and usages.
- Detecting semantic errors.
- Generating Three-Address Code (TAC).
- Creating temporary variables and labels.
- Implementing Intermediate Representation (IR).
- Optimizing the generated intermediate code.
- Passing optimized IR to the code generation module.

## 3. Semantic Analysis

Semantic Analysis checks whether the program follows the language's semantic rules after syntax analysis.

### Main Functions

- Checks whether variables are declared before use.
- Detects duplicate variable declarations.
- Validates variable references.
- Checks assignment statements.
- Identifies semantic errors in expressions.
- Reports errors with appropriate source locations.

### Example

**Input:**
```c
int a;
a = 10;
int b;
b = a + 5;
```

**Invalid Input:**
```c
x = 10;
```

**Expected Result:**

The compiler reports an undeclared variable error because `x` has not been declared.

## 4. Symbol Table

The Symbol Table stores information about identifiers used in the program.

### Information Stored

- Variable name
- Data type
- Scope level
- Declaration information

### Main Operations

1. Initialize the symbol table.
2. Insert a new variable.
3. Search for an existing variable.
4. Check duplicate declarations.
5. Enter a new scope.
6. Exit the current scope.
7. Free allocated memory.

### Example

**Input:**
```c
int a;
int b;
int c;
```

**Symbol Table:**

| Variable | Data Type |
|---|---|
| a | int |
| b | int |
| c | int |

The symbol table helps the compiler identify declared variables and detect invalid variable references.

## 5. Intermediate Representation (IR) Generation

Intermediate Representation is a simplified form of the source program generated after semantic analysis.

This project uses Three-Address Code (TAC) to represent expressions and statements.

### Objectives

- Convert AST nodes into intermediate instructions.
- Simplify complex expressions into smaller operations.
- Generate temporary variables.
- Generate labels for control flow.
- Prepare code for optimization and code generation.

### Example

**Source Code:**
```c
int a = 10;
int b = 20;
int c = a + b;
```

**Illustrative Three-Address Code:**
```text
MOV 10 -> a
MOV 20 -> b
ADD a, b -> t0
MOV t0 -> c
```

Here, `t0` is a temporary variable used to store the intermediate result.

## 6. IR Optimization

IR Optimization improves the intermediate code by reducing unnecessary operations and improving execution efficiency without changing the program's intended result.

### Optimization Techniques

- Constant Folding
- Constant Propagation
- Algebraic Simplification
- Removal of unnecessary operations, where supported.

### Example: Constant Folding

**Before Optimization:**
```text
ADD 10, 20 -> t0
MOV t0 -> a
```

**After Optimization:**
```text
MOV 30 -> a
```

The compiler evaluates constant expressions during compilation instead of runtime.

## 7. Module Integration

Member 2 connects the parser module with the code generation module.

### Workflow

1. Receives the Abstract Syntax Tree (AST) from Member 1.
2. Performs semantic analysis.
3. Uses the symbol table to validate identifiers.
4. Generates Intermediate Representation.
5. Optimizes the generated IR.
6. Passes optimized IR to Member 3 for code generation.

## 8. Technologies Used

- Programming Language: C
- Data Structures: Arrays and structures
- Compiler Concepts: Semantic Analysis, Symbol Table, AST, Three-Address Code
- Optimization: Constant Folding and Constant Propagation
- Build System: CMake
- Version Control: Git and GitHub

## 9. Conclusion

Member 2 implements the middle-end of the Embedded C Subset Compiler. Semantic Analysis ensures that the source program follows the language rules, the Symbol Table manages identifiers, IR Generation converts the AST into intermediate instructions, and IR Optimization improves the generated code.

These components provide the optimized intermediate representation required by the code generation module.