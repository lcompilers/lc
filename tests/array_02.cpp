#include <stdio.h>

#define M 4
#define N 10
#define NUM 4

float matmul_test(int m, int n, int nums) {
    float arr1d[n];
    float arr2d[nums][m][n];
    float sum;
    float output = (float) 0.0; // TODO: Remove float cast
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

    sum = (float) 0.0; // TODO: Remove float cast
    for( int num1 = 0; num1 < nums; num1++ ) {
        for( int i1 = 0; i1 < M; i1++ ) {
            output = (float) 0.0; // TODO: Remove float cast
            for( int j1 = 0; j1 < N; j1++ ) {
                output += arr2d[num1][i1][j1] * arr1d[j1];
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
