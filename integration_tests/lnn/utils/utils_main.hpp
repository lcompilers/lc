#include <vector>

double normalize(double value, double leftMin, double leftMax, double rightMin, double rightMax) {
    // Figure out how 'wide' each range is
    double leftSpan = leftMax - leftMin;
    double rightSpan = rightMax - rightMin;

    // Convert the left range into a 0-1 range (float)
    double valueScaled = (value - leftMin) / leftSpan;

    // Convert the 0-1 range into a value in the right range.
    return rightMin + (valueScaled * rightSpan);
}

void normalize_input_vectors(std::vector<std::vector<double>>& input_vectors) {
    int32_t rows = input_vectors.size();
    int32_t cols = input_vectors[0].size();

    int32_t j;
    for( j = 0; j < cols; j++ ) {
        double colMinVal = input_vectors[0][j];
        double colMaxVal = input_vectors[0][j];
        int32_t i;
        for( i = 0; i < rows; i++ ) {
            if( input_vectors[i][j] > colMaxVal ) {
                colMaxVal = input_vectors[i][j];
            }
            if( input_vectors[i][j] < colMinVal ) {
                colMinVal = input_vectors[i][j];
            }
        }

        for( i = 0; i < rows; i++ ) {
            input_vectors[i][j] = normalize(input_vectors[i][j], colMinVal, colMaxVal, -1.0, 1.0);
        }
    }
}

void normalize_output_vector(std::vector<double>& output_vector) {
    int32_t rows = output_vector.size();
    double colMinVal = output_vector[0];
    double colMaxVal = output_vector[0];
    int32_t i;
    for( i = 0; i < rows; i++ ) {
        if( output_vector[i] > colMaxVal ) {
            colMaxVal = output_vector[i];
        }
        if( output_vector[i] < colMinVal ) {
            colMinVal = output_vector[i];
        }
    }

    for( i = 0; i < rows; i++ ) {
        output_vector[i] = normalize(output_vector[i], colMinVal, colMaxVal, -1.0, 1.0);
    }
}
