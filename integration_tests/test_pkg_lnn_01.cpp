#include "lnn/perceptron/perceptron_main.hpp"
#include "lnn/utils/utils_main.hpp"
#include "lpdraw/draw.hpp"

#define assert_(cond) if( !(cond) ) { \
    exit(2); \
} \

double compute_decision_boundary(struct Perceptron& p, double x) {
    double bias = p.weights[-1];
    double slope = (-p.weights[0] / p.weights[1]);
    double intercept = (-bias / p.weights[1]);
    return slope * x + intercept;
}

void plot_graph(struct Perceptron& p, std::vector<std::vector<double>>& input_vectors, std::vector<int32_t>& outputs) {
    const int32_t Width = 500; // x-axis limits [0, 499]
    const int32_t Height = 500; // y-axis limits [0, 499]
    xt::xtensor<int32_t, 2> Screen = xt::empty<int32_t>({Height, Width});
    Clear(Height, Width, Screen);

    double x1 = 2.0;
    double y1 = compute_decision_boundary(p, x1);
    double x2 = -2.0;
    double y2 = compute_decision_boundary(p, x2);

    // center the graph using the following offset
    double scale_offset = Width / 4;
    double shift_offset = Width / 2;
    x1 *= scale_offset;
    y1 *= scale_offset;
    x2 *= scale_offset;
    y2 *= scale_offset;

    // print (x1, y1, x2, y2)
    Line(Height, Width, Screen, int(x1 + shift_offset), int(y1 + shift_offset), int(x2 + shift_offset), int(y2 + shift_offset));

    int32_t i;
    int32_t point_size = 5;
    for( i = 0; i < input_vectors.size(); i++ ) {
        input_vectors[i][0] *= scale_offset;
        input_vectors[i][1] *= scale_offset;
        input_vectors[i][0] += shift_offset;
        input_vectors[i][1] += shift_offset;
        if( outputs[i] == 1 ) {
            int32_t x = input_vectors[i][0];
            int32_t y = input_vectors[i][1];
            Line(Height, Width, Screen, x - point_size, y, x + point_size, y);
            Line(Height, Width, Screen, x, y - point_size, x, y + point_size);
        } else {
            Circle(Height, Width, Screen, int32_t(input_vectors[i][0]), int32_t(input_vectors[i][1]), double(point_size));
        }
    }

    Display(Height, Width, Screen);
}

void main0() {
    struct Perceptron p;
    p.no_of_inputs = 0;
    p.weights = {0.0};
    p.learn_rate = 0.0;
    p.iterations_limit = 0.0;
    p.des_accuracy = 0.0;
    p.cur_accuracy = 0.0;
    p.epochs_cnt = 0;

    init_perceptron(p, 2, 0.05, 10000, 90.0);
    print_perceptron(p);
    std::cout<<"=================================\n";

    std::vector<std::vector<double>> input_vectors = {{-1.0, -1.0}, {-1.0, 1.0}, {1.0, -1.0}, {1.0, 1.0}};
    std::vector<int32_t> outputs = {1, 1, 1, -1};

    normalize_input_vectors(input_vectors);
    train_dataset(p, input_vectors, outputs);
    print_perceptron(p);

    assert_( p.cur_accuracy > 50.0 );
    assert_( p.epochs_cnt > 1 );

    plot_graph(p, input_vectors, outputs);
}

void main1() {
    struct Perceptron p;
    p.no_of_inputs = 0;
    p.weights = {0.0};
    p.learn_rate = 0.0;
    p.iterations_limit = 0.0;
    p.des_accuracy = 0.0;
    p.cur_accuracy = 0.0;
    p.epochs_cnt = 0;

    init_perceptron(p, 2, 0.05, 10000, 90.0);
    print_perceptron(p);
    std::cout<<"=================================\n";

    std::vector<std::vector<double>> input_vectors = {{-1.0, -1.0}, {-1.0, 1.0}, {1.0, -1.0}, {1.0, 1.0}, {1.5, 1.0}};
    std::vector<int32_t> outputs = {1, 1, -1, 1, -1};

    normalize_input_vectors(input_vectors);
    train_dataset(p, input_vectors, outputs);
    print_perceptron(p);

    assert_( p.cur_accuracy > 50.0 );
    assert_( p.epochs_cnt > 1 );

    plot_graph(p, input_vectors, outputs);
}

int main() {
    main0();
    main1();
}
