# Object-Oriented Programming Guidelines for Real-Time Systems

## 1. Encapsulation & Abstraction

- All direct hardware register accesses and HAL function calls must be strictly hidden inside private class methods.
- Member variables must always be `private` or `protected`. State changes should be managed via `inline` getters and setters to avoid function call overhead.

## 2. Inheritance & Polymorphism

- Dynamic polymorphism (`virtual` functions) is permitted but must be used judiciously to ensure deterministic execution times.
- Avoid deep inheritance hierarchies to minimize VTable lookup overhead and memory footprint.

## 3. Memory Management

- **Dynamic memory allocation (`new`, `delete`, `malloc`, `free`) is STRICTLY PROHIBITED at runtime.**
- All objects must be instantiated statically (Global/BSS scope) or allocated on the stack.
- In the FreeRTOS environment, object lifetimes and stack sizes must be meticulously calculated and managed. Use memory pools if dynamic-like behavior is absolutely necessary.