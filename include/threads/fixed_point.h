#define F (1 << 14) // fixed point 1
#define INT_MAX ((1 << 31) - 1)
#define INT_MIN (-(1 << 31))

// x and y denote fixed_point numbers in 17.14 format
// n is an integer

int int_to_fp(int n);         // integer를 fixed point로 전환
int fp_to_int_round(int x);   // FP를 int로 전환 (반올림)
int fp_to_int(int x);         // FP를 int로 전환 (버림)
int add_fp(int x, int y);     // FP의 덧셈
int add_mixed(int x, int n);  // FP와 int의 덧셈
int sub_fp(int x, int y);     // FP의 뺄셈(x-y)
int sub_mixed(int x, int n);  // FP와 int의 뺄셈(x-n)
int mult_fp(int x, int y);    // FP의 곱셈
int mult_mixed(int x, int n); // FP와 int의 곱셈
int div_fp(int x, int y);     // FP의 나눗셈(x/y)
int div_mixed(int x, int n);  // FP와 int 나눗셈(x/n)


int int_to_fp(int n) // integer를 fixed point로 전환
{
    return n * F;
}
int fp_to_int_round(int x) // FP를 int로 전환 (반올림)
{
    return x / F;
}
int fp_to_int(int x) // FP를 int로 전환 (버림)
{
    if (x >= 0)
        return (x + F / 2) / F;
    else
        return (x - F / 2) / F;
}
int add_fp(int x, int y) // FP의 덧셈
{
    return x + y;
}
int add_mixed(int x, int n) // FP와 int의 덧셈
{
    return x + n * F;
}
int sub_fp(int x, int y) // FP의 뺄셈(x-y)
{
    return x - y;
}
int sub_mixed(int x, int n) // FP와 int의 뺄셈(x-n)
{
    return x - n * F;
}
int mult_fp(int x, int y) // FP의 곱셈
{
    return ((int64_t)x) * y / F;
}
int mult_mixed(int x, int n) // FP와 int의 곱셈
{
    return x * n;
}
int div_fp(int x, int y) // FP의 나눗셈(x/y)
{
    return ((int64_t)x) * F / y;
}
int div_mixed(int x, int n) // FP와 int 나눗셈(x/n)
{
    return x / n;
}
