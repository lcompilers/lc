#include <vector>
#include <iostream>

#define assert(cond) if( !(cond) ) { \
    exit(2); \
} \

void check_mat_and_vec(std::vector<std::vector<double>>& mat, std::vector<double>& vec) {
    int32_t rows = mat.size();
    int32_t cols = vec.size();
    int32_t i;
    int32_t j;

    for( i = 0; i < rows; i++ ) {
        for( j = 0; j < cols; j++ ) {
            assert( mat[i][j] == double(i + j) );
        }
    }

    for( i = 0; i < cols; i++ ) {
        assert( vec[i] == 2.0 * float(i) );
    }
}

void test_list_of_lists() {
    std::vector<std::vector<std::vector<std::vector<double>>>> arrays = {};
    std::vector<std::vector<std::vector<double>>> array = {};
    std::vector<std::vector<double>> mat = {};
    std::vector<double> vec = {};
    int32_t rows = 10;
    int32_t cols = 5;
    int32_t i;
    int32_t j;
    int32_t k;
    int32_t l;

    for( i = 0; i < rows; i++ ) {
        for( j = 0; j < cols; j++ ) {
            vec.push_back(double(i + j));
        }
        mat.push_back(vec);
        vec.clear();
    }

    for( i = 0; i < cols; i++ ) {
        vec.push_back(2.0 * double(i));
    }

    check_mat_and_vec(mat, vec);

    for( k = 0; k < rows; k++ ) {
        array.push_back(mat);
        for( i = 0; i < rows; i++ ) {
            for( j = 0; j < cols; j++ ) {
                mat[i][j] += double(1);
            }
        }
    }

    for( k = 0; k < rows; k++ ) {
        for( i = 0; i < rows; i++ ) {
            for( j = 0; j < cols; j++ ) {
                assert( mat[i][j] - array[k][i][j] == double(rows - k) );
            }
        }
    }

    for( l = 0; l < 2 * rows; l++ ) {
        arrays.push_back(array);
        for( i = 0; i < rows; i++ ) {
            for( j = 0; j < rows; j++ ) {
                for( k = 0; k < cols; k++ ) {
                    array[i][j][k] += 1.0;
                }
            }
        }
    }

    for( l = 0; l < 2 * rows; l++ ) {
        for( i = 0; i < rows; i++ ) {
            for( j = 0; j < rows; j++ ) {
                for( k = 0; k < cols; k++ ) {
                    std::cout << (array[i][j][k] - arrays[l][i][j][k]);
                    std::cout << 2 * rows - l;
                    assert( array[i][j][k] - arrays[l][i][j][k] == double(2 * rows - l) );
                }
            }
        }
    }
}

int main() {

test_list_of_lists();

}
