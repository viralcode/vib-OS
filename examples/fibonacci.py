# Fibonacci Sequence in Python for Vib-OS
# Run with: run fibonacci.py

def fibonacci(n):
    """Generate first n Fibonacci numbers"""
    if n <= 0:
        return []
    elif n == 1:
        return [0]
    
    fib = [0, 1]
    for i in range(2, n):
        fib.append(fib[i-1] + fib[i-2])
    return fib

def main():
    print("Fibonacci Sequence Generator")
    print("-" * 30)
    
    count = 10
    sequence = fibonacci(count)
    
    print(f"First {count} Fibonacci numbers:")
    for i, num in enumerate(sequence):
        print(f"  F({i}) = {num}")
    
    print(f"\nSum: {sum(sequence)}")

if __name__ == "__main__":
    main()
