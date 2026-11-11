export const EXAMPLES = [
  {
    name: "While loop (project example)",
    code: "int x = 2;\nwhile (x < 5) {\n  x = x + 1;\n}\n",
  },
  {
    name: "Sum of 1..10",
    code: "int i = 1;\nint sum = 0;\nwhile (i <= 10) {\n  sum = sum + i;\n  i = i + 1;\n}\n",
  },
  {
    name: "Factorial of 5",
    code: "int n = 5;\nint f = 1;\nwhile (n > 1) {\n  f = f * n;\n  n = n - 1;\n}\n",
  },
  {
    name: "If / else",
    code: "int a = 7;\nint b = 3;\nint max = 0;\nif (a > b) {\n  max = a;\n} else {\n  max = b;\n}\n",
  },
  {
    name: "Optimizer demo (constant folding)",
    code: "int a = 10 + 20;\nint b = a * 2;\nint c = (b - 10) / 5;\n",
  },
  {
    name: "Nested loops",
    code: "int i = 0;\nint count = 0;\nwhile (i < 3) {\n  int j = 0;\n  while (j < 4) {\n    count = count + 1;\n    j = j + 1;\n  }\n  i = i + 1;\n}\n",
  },
  {
    name: "ERROR: undeclared variable",
    code: "int a = 5;\nb = a + 1;\n",
  },
  {
    name: "ERROR: syntax",
    code: "int a = ;\nint b = 3\n",
  },
  {
    name: "ERROR: division by zero (runtime)",
    code: "int a = 10;\nint z = 0;\nint r = a / z;\n",
  },
  {
    name: "ERROR: infinite loop (step limit)",
    code: "int x = 1;\nwhile (x) {\n  x = 1;\n}\n",
  },
];