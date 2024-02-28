#ifndef INTEGRATION_TESTS_NN_01_H
#define INTEGRATION_TESTS_NN_01_H

#include <xtensor/xtensor.hpp>
#include <xtensor/xfixed.hpp>
#include "xtensor/xio.hpp"
#include "xtensor/xview.hpp"
#include <cmath>

// Neuron.begin
struct Neuron_t {
    size_t m_nWeights;
    xt::xtensor<float, 1> m_weights;
    float m_activation;
    float m_output;
    float m_delta;
};

void init_Neuron_t(Neuron_t& neuron, int n_weights);

void activate(Neuron_t& neuron, const xt::xtensor<float, 1>& inputs);

void transfer(Neuron_t& neuron);

float transfer_derivative(Neuron_t& neuron) {
    return static_cast<float>(neuron.m_output * (1.0 - neuron.m_output));
};

// return mutable reference to the neuron weights
xt::xtensor<float, 1>& get_weights(Neuron_t& neuron) {
    return neuron.m_weights;
};

float get_output(Neuron_t& neuron) {
    return neuron.m_output;
};

float get_activation(Neuron_t& neuron) {
    return neuron.m_activation;
};

float get_delta(Neuron_t& neuron) {
    return neuron.m_delta;
};

void set_delta(Neuron_t& neuron, float delta) {
    neuron.m_delta = delta;
};

void initWeights(Neuron_t& neuron, int n_weights);
// Neuron.end

// Layer.begin
struct Layer_t {
    xt::xtensor<Neuron_t, 1> m_neurons;
};

void init_Layer_t(Layer_t& layer, int n_neurons, int n_weights);

xt::xtensor<Neuron_t, 1> get_neurons(Layer_t& layer) {
    return layer.m_neurons;
};

void initNeurons(Layer_t& layer, int n_neurons, int n_weights);
// Layer.end

// Network.begin
struct Network_t {
    size_t m_nLayers, current_layer;
    xt::xtensor<Layer_t, 1> m_layers;
};

void init_Network_t(Network_t& network);

void initialize_network(Network_t& network, int n_inputs, int n_hidden, int n_outputs);

void add_layer(Network_t& network, int n_neurons, int n_weights);
xt::xtensor<float, 1> forward_propagate(Network_t& network, xt::xtensor<float, 1>& inputs);
void backward_propagate_error(Network_t& network, xt::xtensor<float, 1>& expected);
void update_weights(Network_t& network, const xt::xtensor<float, 1>& inputs, float l_rate);
void train(Network_t& network, xt::xtensor<float, 2>& trainings_data, float l_rate, size_t n_epoch, size_t n_outputs);
int predict(Network_t& network, xt::xtensor<float, 1>& input);

void display_human(Network_t& network);
// Network.end

#endif
