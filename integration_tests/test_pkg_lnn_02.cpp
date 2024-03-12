#include "lnn/regression/regression_main.hpp"
#include "lnn/utils/utils_main.hpp"
#include "lpdraw/draw.hpp"

#define assert_(cond) if( !(cond) ) { \
    exit(2); \
} \

double compute_decision_boundary(struct Perceptron& p, double x) {
    double bias = p.weights[1];
    double slope = p.weights[0];
    double intercept = bias;
    return slope * x + intercept;
}

void plot_graph(struct Perceptron& p, std::vector<std::vector<double>>& input_vectors, std::vector<double>& outputs) {
    const int32_t Width = 500; // x-axis limits [0, 499]
    const int32_t Height = 500; // # y-axis limits [0, 499]
    xt::xtensor<int32_t, 2> Screen = xt::empty<int32_t>({Height, Width});
    Clear(Height, Width, Screen);

    double x1 = 1.0;
    double y1 = compute_decision_boundary(p, x1);
    double x2 = -1.0;
    double y2 = compute_decision_boundary(p, x2);

    // center the graph using the following offset
    double scale_offset = Width / 4;
    double shift_offset = Width / 2;
    x1 *= scale_offset;
    y1 *= scale_offset;
    x2 *= scale_offset;
    y2 *= scale_offset;

    // print (x1, y1, x2, y2)
    Line(Height, Width, Screen, int32_t(x1 + shift_offset), int32_t(y1 + shift_offset), int32_t(x2 + shift_offset), int32_t(y2 + shift_offset));

    int32_t i;
    int32_t point_size = 5;
    for( i = 0; i < input_vectors.size(); i++ ) {
        input_vectors[i][0] *= scale_offset;
        input_vectors[i][0] += shift_offset;
        outputs[i] *= scale_offset;
        outputs[i] += shift_offset;

        Circle(Height, Width, Screen, int32_t(input_vectors[i][0]), int32_t(outputs[i]), double(point_size));
    }

    Display(Height, Width, Screen);
}

void main0() {
    struct Perceptron p;
    p.no_of_inputs = 0;
    p.weights = {0.0};
    p.learn_rate = 0.0;
    p.iterations_limit = 0.0;
    p.err_limit = 0.0;
    p.err = 0.0;
    p.epochs_cnt = 0;

    init_perceptron(p, 1, 0.0005, 10000, 1e-16);

    std::vector<std::vector<double>> input_vectors = {{1.1}, {1.3}, {1.5}, {2.0}, {2.2}, {2.9}, {3.0}, {3.2},
                                                      {3.2}, {3.7}, {3.9}, {4.0}, {4.0}, {4.1}, {4.5}, {4.9},
                                                      {5.1}, {5.3}, {5.9}, {6.0}, {6.8}, {7.1}, {7.9}, {8.2},
                                                      {8.7}, {9.0}, {9.5}, {9.6}, {10.3}, {10.5}, {11.2}, {11.5},
                                                      {12.3}, {12.9}, {13.5}};
    std::vector<double> outputs = {39343.0, 46205.0, 37731.0, 43525.0, 39891.0, 56642.0, 60150.0, 54445.0, 64445.0,
                                   57189.0, 63218.0, 55794.0, 56957.0, 57081.0, 61111.0, 67938.0, 66029.0, 83088.0,
                                   81363.0, 93940.0, 91738.0, 98273.0, 101302.0, 113812.0, 109431.0, 105582.0, 116969.0,
                                   112635.0, 122391.0, 121872.0, 127345.0, 126756.0, 128765.0, 135675.0, 139465.0};

    normalize_input_vectors(input_vectors);
    normalize_output_vector(outputs);

    train_dataset(p, input_vectors, outputs);
    print_perceptron(p);

    assert_(abs(p.weights[0] - (1.0640975812232145)) <= 1e-12);
    assert_(abs(p.weights[1] - (0.0786977829749839)) <= 1e-12);
    assert_(abs(p.err - (0.4735308448814293)) <= 1e-12);
    assert_(p.epochs_cnt == 4515);

    plot_graph(p, input_vectors, outputs);
}

void main1() {
    struct Perceptron p;
    p.no_of_inputs = 0;
    p.weights = {0.0};
    p.learn_rate = 0.0;
    p.iterations_limit = 0.0;
    p.err_limit = 0.0;
    p.err = 0.0;
    p.epochs_cnt = 0;
    init_perceptron(p, 1, 0.0005, 10000, 1e-16);

    std::vector<std::vector<double>> input_vectors = {{1.0}, {3.0}, {7.0}};
    std::vector<double> outputs = {8.0, 4.0, -2.0};

    normalize_input_vectors(input_vectors);
    normalize_output_vector(outputs);

    train_dataset(p, input_vectors, outputs);
    print_perceptron(p);

    assert_(abs(p.weights[0] - (-0.9856542200697508)) <= 1e-12);
    assert_(abs(p.weights[1] - (-0.0428446744717655)) <= 1e-12);
    assert_(abs(p.err - 0.011428579012311327) <= 1e-12);
    assert_(p.epochs_cnt == 10000);

    plot_graph(p, input_vectors, outputs);
}

int main() {

main0();
main1();

return 0;

}
