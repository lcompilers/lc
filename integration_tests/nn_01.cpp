#include "nn_01.h"

void init_Neuron_t(Neuron_t& neuron, int n_weights) {
    initWeights(neuron, n_weights);
    neuron.m_nWeights = n_weights;
    neuron.m_activation = 0;
    neuron.m_output = 0;
    neuron.m_delta = 0;
}

void initWeights(Neuron_t& neuron, int n_weights) {
    for (int w = 0; w < n_weights; w++) {
        neuron.m_weights[w] = 0.0;
    }
}

void activate(Neuron_t& neuron, xt::xtensor<float, 1>& inputs) {
    neuron.m_activation = neuron.m_weights[neuron.m_nWeights-1];
    for (size_t i = 0; i < neuron.m_nWeights-1; i++)
    {
        neuron.m_activation += neuron.m_weights[i] * inputs[i];
    }
}

void transfer(Neuron_t& neuron) {
    neuron.m_output = 1.0f / (1.0f + exp(-neuron.m_activation));
}

void init_Layer_t(Layer_t& layer, int n_neurons, int n_weights) {
    initNeurons(layer, n_neurons, n_weights);
}

void initNeurons(Layer_t& layer, int n_neurons, int n_weights) {
    for (int n = 0; n < n_neurons; n++) {
        Neuron_t neuron;
        init_Neuron_t(neuron, n_weights);
        layer.m_neurons[n] = neuron;
    }
}

void init_Network_t(Network_t& network) {
    network.m_nLayers = 0;
}

void initialize_network(Network_t& network, int n_inputs, int n_hidden, int n_outputs) {
    add_layer(network, n_hidden, n_inputs + 1);
    add_layer(network, n_outputs, n_hidden + 1);
}

void add_layer(Network_t& network, int n_neurons, int n_weights) {
    network.current_layer++;
    Layer_t layer;
    init_Layer_t(layer, n_neurons, n_weights);
    network.m_layers[network.current_layer] = layer;
}

xt::xtensor<float, 1> forward_propagate(Network_t& network, xt::xtensor<float, 1>& inputs) {
    for (size_t i = 0; i < network.m_nLayers; i++) {
        xt::xtensor<Neuron_t, 1> layer_neurons = get_neurons(network.m_layers[i]);
        xt::xtensor<float, 1> new_inputs = xt::empty<float>({layer_neurons.size()});
        for (size_t n = 0; n < layer_neurons.size(); n++) {
            activate(layer_neurons[n], inputs);
            transfer(layer_neurons[n]);
            new_inputs[n] = get_output(layer_neurons[n]);
        }
        inputs = new_inputs;
    }
    return inputs;
}

void backward_propagate_error(Network_t& network, xt::xtensor<float, 1>& expected) {
    for (size_t i = network.m_nLayers; i > 0;) {
        i--;
        xt::xtensor<Neuron_t, 1> layer_neurons = get_neurons(network.m_layers[i]);
        for (size_t n = 0; n < layer_neurons.size(); n++) {
            float error = 0.0;
            if (i == network.m_nLayers - 1)
            {
                error = expected[n] - get_output(layer_neurons[n]);
            }
            else {
                xt::xtensor<Neuron_t, 1> neus = get_neurons(network.m_layers[i + 1]);
                for (size_t j = 0; j < neus.size(); j++) {
                    error += (get_weights(neus[j])[n] * get_delta(neus[j]));
                }
            }
            set_delta(layer_neurons[n], error * transfer_derivative(layer_neurons[n]));
        }
    }
}

void update_weights(Network_t& network, const xt::xtensor<float, 1>& inputs, float l_rate) {
    for (size_t i = 0; i < network.m_nLayers; i++) {
        xt::xtensor<float, 1> new_inputs;
        if (i != 0) {
            Layer_t layer = network.m_layers[i-1];
            xt::xtensor<Neuron_t, 1> neurons = get_neurons(layer);
            new_inputs = xt::empty<float>({neurons.size()});
            for (size_t j = 0; j < neurons.size(); j++) {
                new_inputs[j] = get_output(neurons[j]);
            }
        } else {
            new_inputs = xt::view(inputs, xt::range(0, inputs.size() - 1));
        }

        xt::xtensor<Neuron_t, 1> layer_neurons = get_neurons(network.m_layers[i]);

        for (size_t n = 0; n < layer_neurons.size(); n++) {
            xt::xtensor<float, 1> weights = get_weights(layer_neurons[n]);
            for (size_t j = 0; j < new_inputs.size(); j++) {
                weights[j] += l_rate * get_delta(layer_neurons[n]) * new_inputs[j];
            }
            weights[weights.size() - 1] += l_rate * get_delta(layer_neurons[n]);
        }
    }
}

void train(Network_t& network, xt::xtensor<float, 2>& trainings_data, float l_rate, size_t n_epoch, size_t n_outputs) {
    for (size_t e = 0; e < n_epoch; e++) {
        float sum_error = 0;
        for (size_t i = 0; i < trainings_data.shape(0); i++) {
            xt::xtensor<float, 1> row = xt::view(trainings_data, i);
            xt::xtensor<float, 1> outputs = forward_propagate(network, row);
            xt::xtensor<float, 1> expected = xt::empty<float>({n_outputs});
            expected(trainings_data(i, trainings_data.shape(1) - 1)) = 1.0;
            for (size_t x = 0; x < n_outputs; x++) {
                sum_error += static_cast<float>(pow((expected[x] - outputs[x]), 2));
            }
            backward_propagate_error(network, expected);
            update_weights(network, xt::view(trainings_data, i), l_rate);
        }
        std::cout << "[>] epoch=" << e << ", l_rate=" << l_rate << ", error=" << sum_error << std::endl;
    }
}

int predict(Network_t& network, xt::xtensor<float, 1>& input) {
    xt::xtensor<float, 1> outputs = forward_propagate(network, input);
    return xt::amax(outputs)() - outputs[0];
}

void display_human(Network_t& network) {
    std::cout << "[Network] (Layers: " << network.m_nLayers << ")" << std::endl;


    std::cout << "{" << std::endl;
    for (size_t l = 0; l < network.m_layers.size(); l++) {
        Layer_t layer = network.m_layers[l];
        std::cout << "\t (Layer " << l << "): {";
        for (size_t i = 0; i < get_neurons(layer).size(); i++) {
            Neuron_t neuron = get_neurons(layer)[i];
            std::cout << "<(Neuron " << i << "): [ weights={";
            xt::xtensor<float, 1> weights = get_weights(neuron);
            for (size_t w = 0; w < weights.size(); ++w) {
                std::cout << weights[w];
                if (w < weights.size() - 1) {
                    std::cout << ", ";
                }
            }
            std::cout << "}, output=" << get_output(neuron) << ", activation=" << get_activation(neuron) << ", delta=" << get_delta(neuron);
            std::cout << "]>";
            if (i < get_neurons(layer).size() - 1) {
                std::cout << ", ";
            }
        }
        std::cout << "}";
        if (l < network.m_layers.size() - 1) {
            std::cout << ", ";
        }
        std::cout << std::endl;
    }
    std::cout << "}" << std::endl;
}

xt::xtensor<float, 2> generate_data();
xt::xtensor<float, 1> evaluate_network(const xt::xtensor<float, 2>& dataset, int n_folds, float l_rate, int n_epoch, int n_hidden);
float accuracy_metric(const xt::xtensor<int, 1>& expect, const xt::xtensor<int, 1>& predict);

int main() {
    std::cout << "Neural Network with Backpropagation in C++ from scratch" << std::endl;

    xt::xtensor<float, 2> csv_data;
    int n_folds = 5;
    float l_rate = 0.3f;
    int n_epoch = 500;
    int n_hidden = 5;

    csv_data = generate_data();
    xt::xtensor<float, 1> scores = evaluate_network(csv_data, n_folds, l_rate, n_epoch, n_hidden);
    float mean = xt::sum(scores)() / static_cast<float>(scores.size());
    std::cout << "Mean accuracy: " << mean << std::endl;

    return 0;
}

xt::xtensor<float, 1> evaluate_network(const xt::xtensor<float, 2>& dataset, int n_folds, float l_rate, int n_epoch, int n_hidden) {
    xt::xtensor<float, 3> dataset_splits;
    std::vector<float> scores;


    size_t fold_size = static_cast<unsigned int>(dataset.size() / n_folds);
    for (int f = 0; f < n_folds; f++) {
        xt::xtensor<float, 2> fold;
        while (fold.size() < fold_size) {
            int n = rand() % dataset.size();
            std::swap(dataset[n], dataset.back());
            fold.push_back(dataset.back());
            dataset.pop_back();
        }
        dataset_splits.push_back(fold);
    }


    /* Iterate over folds */
    // choose one as test and the rest as training sets
    for (size_t i = 0; i < dataset_splits.size(); i++)
    {
        std::vector<std::vector<std::vector<float>>> train_sets = dataset_splits;
        std::swap(train_sets[i], train_sets.back());
        std::vector<std::vector<float>> test_set = train_sets.back();
        train_sets.pop_back();


        // merge the multiple train_sets into one train set
        std::vector<std::vector<float>> train_set;
        for (auto &s: train_sets)
        {
            for (auto& row : s) {
                train_set.push_back(row);
            }
        }


        // store the expected results
        std::vector<int> expected;
        for (auto& row: test_set)
        {
            expected.push_back(static_cast<int>(row.back()));
            // just ensure that the actual result is not saved in the test data
            row.back() = 42;
        }


        std::vector<int> predicted;


        std::set<float> results;
        for (const auto& r : train_set) {
            results.insert(r.back());
        }
        int n_outputs = results.size();
        int n_inputs = train_set[0].size() - 1;


        /* Backpropagation with stochastic gradient descent */
        Network* network = new Network();
        network->initialize_network(n_inputs, n_hidden, n_outputs);
        network->train(train_set, l_rate, n_epoch, n_outputs);


        for (const auto& row: test_set)
        {
            predicted.push_back(network->predict(row));
        }


        scores.push_back(accuracy_metric(expected, predicted));
    }


    return scores;
}
