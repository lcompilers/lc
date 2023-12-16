#include <stdio.h>

int main()
{
    int arr1d[5] = { 1, 2, 3, 4, 5 };
    int arr2d[4][3][2] = {
        {{1, 2}, {3, 4}, {5, 6}},
        {{8, 9}, {10, 11}, {12, 13}},
        {{14, 15}, {16, 17}, {18, 19}},
        {{20, 21}, {22, 23}, {24, 25}}
    };
    int sum1d = 0, sum2d = 0;

    for( int i = 0; i < 5; i++ ) {
        sum1d += arr1d[i];
    }

    for( int j = 0; j < 4; j++ ) {
        for( int k = 0; k < 3; k++ ) {
            for( int l = 0; l < 2; l++ ) {
                sum2d += arr2d[j][k][l];
            }
        }
    }

    printf("%d %d\n", sum1d, sum2d);

    return 0;
}
