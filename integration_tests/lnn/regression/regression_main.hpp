#include <iostream>

struct Perceptron {
    int32_t no_of_inputs;
    std::vector<double> weights;
    double learn_rate;
    int32_t iterations_limit;
    double err_limit;
    double err;
    int32_t epochs_cnt;
};

double predict_perceptron(struct Perceptron& p, std::vector<double>& input_vector);
double activation_function(double value);
double test_perceptron(struct Perceptron& p, std::vector<std::vector<double>>& input_vectors, std::vector<double>& outputs);

std::vector<double> get_inp_vec_with_bias(std::vector<double>& a) {
    std::vector<double> b = {};
    size_t i;
    for( i = 0; i < a.size(); i++ ) {
        b.push_back(a[i]);
    }
    b.push_back(1.0);
    return b;
}

std::vector<double> init_weights(int32_t size) {
    std::vector<double> weights = {};
    int32_t i;
    for( i = 0; i < size; i++ ) {
        weights.push_back(0.0);
    }
    weights.push_back(0.0);
    return weights;
}

void init_perceptron(struct Perceptron& p, int32_t n, double rate, int32_t iterations_limit, double err_limit) {
    p.no_of_inputs = n;
    p.weights = init_weights(n);
    p.learn_rate = rate;
    p.iterations_limit = iterations_limit;
    p.err_limit = err_limit;
    p.err = 1.0;
    p.epochs_cnt = 0;
}

void train_perceptron(struct Perceptron& p, std::vector<double>& input_vector, double actual_output) {
    double predicted_output = predict_perceptron(p, input_vector);
    double error = actual_output - predicted_output;
    int32_t i;
    for( i = 0; i < input_vector.size(); i++ ) {
        p.weights[i] += p.learn_rate * error * input_vector[i];
    }
}

double predict_perceptron(struct Perceptron& p, std::vector<double>& input_vector) {
    double weighted_sum = 0.0;
    int32_t i = 0;
    for( i = 0; i < input_vector.size(); i++ ) {
        weighted_sum = weighted_sum + p.weights[i] * input_vector[i];
    }
    return activation_function(weighted_sum);
}

double activation_function(double value) {
    return value;
}

void train_epoch(struct Perceptron& p, std::vector<std::vector<double>>& input_vectors, std::vector<double>& outputs) {
    int32_t i;
    for( i = 0; i < input_vectors.size(); i++ ) {
        std::vector<double> input_vector = get_inp_vec_with_bias(input_vectors[i]);
        if ( predict_perceptron(p, input_vector) != outputs[i] ) {
            train_perceptron(p, input_vector, outputs[i]);
        }
    }
}

void train_dataset(struct Perceptron& p, std::vector<std::vector<double>>& input_vectors, std::vector<double>& outputs) {
    double prev_err = 0.0;
    p.err = 1.0;
    p.epochs_cnt = 0;
    while( abs(p.err - prev_err) >= p.err_limit and p.epochs_cnt < p.iterations_limit ) {
        p.epochs_cnt += 1;
        train_epoch(p, input_vectors, outputs);
        prev_err = p.err;
        p.err = test_perceptron(p, input_vectors, outputs);
    }
}

double test_perceptron(struct Perceptron& p, std::vector<std::vector<double>>& input_vectors, std::vector<double>& outputs) {
    double err = 0.0;
    int32_t i;
    for( i = 0; i < input_vectors.size(); i++ ) {
        std::vector<double> input_vector = get_inp_vec_with_bias(input_vectors[i]);
        err = err + pow((outputs[i] - predict_perceptron(p, input_vector)), 2.0);
    }
    return err;
}

void print_perceptron(struct Perceptron& p) {
    std::cout << "weights = [";
    int32_t i;
    for( i = 0; i < p.no_of_inputs; i++ ) {
        std::cout << p.weights[i] << ", ";
    }
    std::cout << p.weights[p.no_of_inputs] << "(bias)]\n";
    std::cout << "learn_rate = ";
    std::cout << p.learn_rate << "\n";
    std::cout << "error = ";
    std::cout << p.err << "\n";
    std::cout << "epochs_cnt = ";
    std::cout << p.epochs_cnt << "\n";
}
