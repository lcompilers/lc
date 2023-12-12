#include <stdio.h>

#define M 4
#define N 10
#define NUM 4

float matmul_test(int m, int n, int nums) {
    float arr1d[n];
    float arr2d[nums][m][n];
    float sum;
    for( int num = 0; num < nums; num++ ) {
        for( int i = 0; i < m; i++ ) {
            for( int j = 0; j < n; j++ ) {
                arr2d[num][i][j] = (float) (i + j) / (float) (m * n);
            }
        }
    }

    for( int k = 0; k < n; k++ ) {
        arr1d[k] = (float) k / (float) n;
    }

    sum = 0.0;
    for( int num = 0; num < nums; num++ ) {
        for( int i = 0; i < M; i++ ) {
            float output = 0.0;
            for( int j = 0; j < N; j++ ) {
                output += arr2d[num][i][j] * arr1d[j];
            }
            sum += output;
        }
    }
    return sum;
}

int main()
{
    float sum = matmul_test(M, N, NUM);
    printf("%f\n", sum);
    return 0;
}
