#define BOOST_BIND_GLOBAL_PLACEHOLDERS
#include <iostream>
#include <Python.h>
#include <string>
#include <sstream>
#include <cpprest/ws_client.h>
#include <thread>
#include <vector>
#include <algorithm>
#include <functional>
#include <chrono>

#include "boost/property_tree/ptree.hpp"
#include "boost/property_tree/json_parser.hpp"

using namespace boost::placeholders;
using namespace boost::property_tree;

using namespace web;
using namespace web::websockets::client;

// Parses a PyObject result into a 1D vector
std::vector<double> ParsePy(PyObject* pObject) {
    std::vector<double> result;

    // Check if the object is a list or a sequence
    if (PyList_Check(pObject) || PySequence_Check(pObject)) {
        Py_ssize_t size = PySequence_Size(pObject);

        result.reserve(size);

        for (Py_ssize_t i = 0; i < size; ++i) {
            PyObject* pItem = PySequence_GetItem(pObject, i);

            if (PyFloat_Check(pItem) || PyLong_Check(pItem)) {
                double value = PyFloat_AsDouble(pItem);
                result.push_back(value);
            } else {
                std::cerr << "Error: Element at index " << i << " is not a numeric type." << std::endl;
                // Handle the error appropriately
            }

            // Don't forget to decref pItem
            Py_XDECREF(pItem);
        }
    } else {
        std::cerr << "Error: Object is not a list or a sequence." << std::endl;
    }

    return result;
}

// Parses a result PyObject into a 2D vector
std::vector<std::vector<double>> ParsePy2(PyObject* pObject) {
    std::vector<std::vector<double>> result;

    // Check if the object is a list or a sequence
    if (PyList_Check(pObject) || PySequence_Check(pObject)) {
        Py_ssize_t outerSize = PySequence_Size(pObject);

        result.reserve(outerSize);

        for (Py_ssize_t i = 0; i < outerSize; ++i) {
            PyObject* innerList = PySequence_GetItem(pObject, i);

            // Check if the inner object is also a list or sequence
            if (PyList_Check(innerList) || PySequence_Check(innerList)) {
                Py_ssize_t innerSize = PySequence_Size(innerList);

                std::vector<double> innerVector;
                innerVector.reserve(innerSize);

                for (Py_ssize_t j = 0; j < innerSize; ++j) {
                    PyObject* pItem = PySequence_GetItem(innerList, j);

                    if (PyFloat_Check(pItem) || PyLong_Check(pItem)) {
                        double value = PyFloat_AsDouble(pItem);
                        innerVector.push_back(value);
                    } else {
                        std::cerr << "Error: Element at index [" << i << "][" << j << "] is not a numeric type." << std::endl;
                        // Handle the error appropriately
                    }

                    // Don't forget to decref pItem
                    Py_XDECREF(pItem);
                }

                result.push_back(innerVector);
            } else {
                std::cerr << "Error: Inner object at index [" << i << "] is not a list or a sequence." << std::endl;
                // Handle the error appropriately
            }

            // Don't forget to decref innerList
            Py_XDECREF(innerList);
        }
    } else {
        std::cerr << "Error: Object is not a list or a sequence." << std::endl;
    }

    return result;
}

// Converts a 2D vector into a PyObject
PyObject* list2D(const std::vector<std::vector<double>>& cppData) {
    PyObject* pyList = PyList_New(cppData.size());

    for (size_t i = 0; i < cppData.size(); ++i) {
        PyObject* innerList = PyList_New(cppData[i].size());

        for (size_t j = 0; j < cppData[i].size(); ++j) {
            PyObject* value = PyFloat_FromDouble(cppData[i][j]);
            PyList_SET_ITEM(innerList, j, value);
        }

        PyList_SET_ITEM(pyList, i, innerList);
    }

    return pyList;
}

// Converts a 1D vector into a PyObject
PyObject* list1D(const std::vector<double>& cppData) {
    PyObject* pyList = PyList_New(cppData.size());

    for (size_t i = 0; i < cppData.size(); ++i) {
        PyObject* value = PyFloat_FromDouble(cppData[i]);
        PyList_SET_ITEM(pyList, i, value);
    }

    return pyList;
}

// Parses price data into an inputted vector to add the latest price
void Cyclone(ptree dataset, std::vector<double> & price_data)
{
    ptree::const_iterator end = dataset.end();
    bool ticker = false;
    for(ptree::const_iterator it = dataset.begin(); it != end; ++it){
        if(ticker == true){
            if(it->first == "price"){
                price_data.push_back(atof(it->second.get_value<std::string>().c_str()));
            }
        }
        if(it->second.get_value<std::string>() == "ticker"){
            ticker = true;
        }
    }
}

// Converts a string into a JSON object using Boost
ptree JSON(std::string message){
    std::stringstream ss(message);
    ptree data;
    read_json(ss, data);
    return data;
}

// Websocket Client takes the input of the price_data vector and the limit to how big the vector gets
void Socket(std::vector<double> & price_data, int limit){

    // Define url and message to send to Coinbase server to stream ticker data
    std::string url = "wss://ws-feed.exchange.coinbase.com";
    std::string msg = "{\"type\":\"subscribe\", \"product_ids\":[\"BTC-USD\"], \"channels\":[\"ticker\"]}";

    // Declare the websocket client and pass the message
    websocket_client client;
    client.connect(url).wait();

    websocket_outgoing_message out_msg;
    out_msg.set_utf8_message(msg);
    client.send(out_msg);

    while(true){
        client.receive().then([](websocket_incoming_message in_msg){
            return in_msg.extract_string();
        }).then([&](std::string message){
            // Parse the price data and delete first element of vector once limit is reached
            Cyclone(JSON(message), std::ref(price_data));
            if(price_data.size() > limit){
                price_data.erase(price_data.begin());
            }
        }).wait();
    }

    client.close().wait();
}

// Builds a dataframe off price data
void DataFrame(std::vector<double> prices, std::vector<std::vector<double>> & X, std::vector<double> & Y)
{
    // Computes the mean and standard deviation given a vector of price data
    auto stats = [](std::vector<double> windows)
    {
        double mean = 0, stdev = 0;
        for(auto & price : windows){
            mean += price;
        }
        mean /= (double) windows.size();
        for(auto & price : windows){
            stdev += pow(price - mean, 2);
        }
        stdev = sqrt(stdev/((double) windows.size() - 1));
        std::vector<double> result = {mean, stdev};
        return result;
    };

    // Calculates the cumulative rate of return of return off a given vector 
    auto ror = [](std::vector<double> windows)
    {
        double result = 1;
        for(int i = 1; i < windows.size(); ++i){
            result *= windows[i]/windows[i-1];
        }
        return result - 1;
    };

    int window = 15;
    int output = 5;

    std::vector<double> Window, Stats, Temp, OWindow;

    // Computes the input and output vector for the Support Vector Machine
    for(int i = window; i < prices.size() - output; ++i){
        Temp.clear();
        Window = {prices.begin() + (i - window), prices.begin() + i};
        OWindow = {prices.begin() + i, prices.begin() + i + output};
        Stats = stats(Window);

        // Compute the technical indicators
        Temp.push_back(prices[i]);
        Temp.push_back(Stats[0]);
        Temp.push_back(prices[i] - 2.0*Stats[1]);
        Temp.push_back(prices[i] + 2.0*Stats[1]);
        X.push_back(Temp);
        
        // Classification labels for Support Vector Machine
        if(ror(OWindow) > 0){
            Y.push_back(0.0);
        } else {
            Y.push_back(1.0);
        }
    }

    // Adds the last row of data to be predicted by the SVM
    Window = {prices.end() - output, prices.end()};
    Stats = stats(Window);
    Temp.clear();
    Temp.push_back(prices[prices.size() - 1]);
    Temp.push_back(Stats[0]);
    Temp.push_back(prices[prices.size() - 1] - 2.0*Stats[1]);
    Temp.push_back(prices[prices.size() - 1] + 2.0*Stats[1]);
    X.push_back(Temp);

}

// Normalizes the technical analysis dataframe 2D vector
void Normalize(std::vector<std::vector<double>> Inputs, std::vector<std::vector<double>> & NInputs)
{
    // Computes the mean and standard deviation of a given vector
    auto stats = [](std::vector<double> windows)
    {
        double mean = 0, stdev = 0;
        for(auto & price : windows){
            mean += price;
        }
        mean /= (double) windows.size();
        for(auto & price : windows){
            stdev += pow(price - mean, 2);
        }
        stdev = sqrt(stdev/((double) windows.size() - 1));
        std::vector<double> result = {mean, stdev};
        return result;
    };

    // Transposes the 2D vector
    auto transpose = [](std::vector<std::vector<double>> z)
    {
        std::vector<std::vector<double>> L;
        std::vector<double> temp;
        for(int i = 0; i < z[0].size(); ++i){
            temp.clear();
            for(int j = 0; j < z.size(); ++j){
                temp.push_back(z[j][i]);
            }
            L.push_back(temp);
        }
        return L;
    };

    // Normalizes the dataframe with the ZScore method
    Inputs = transpose(Inputs);
    for(int i = 0; i < Inputs.size(); ++i){
        std::vector<double> Stats = stats(Inputs[i]);
        for(int j = 0; j < Inputs[i].size(); ++j){
            Inputs[i][j] = (Inputs[i][j] - Stats[0])/Stats[1];
        }
    }
    // Setting the normalized data equal to the transpose of the inputs
    NInputs = transpose(Inputs);
}

int main()
{
    // Initialize Python Header
    Py_Initialize();

    std::vector<double> pred_results;

    std::vector<std::vector<double>> Inputs, NInputs, Train, Test;
    std::vector<double> prices, outputs;
    int limit = 300;
    int start_limit = 60;

    // Create a thread to stream Bitcoin's price data
    std::thread datafeed(Socket, std::ref(prices), limit);    

    // Import the Support Vector Machine from Scikit-Learn using PyObjects
    PyObject * svm = PyImport_Import(PyUnicode_FromString("sklearn.svm"));
    PyObject * SVC = PyObject_GetAttrString(svm, "SVC");

    PyObject * EARG = PyTuple_New(0);

    // Set the kernel to radial basis function in case data is non linear and also extract the probabilities
    PyObject * init_params = PyDict_New();
    PyDict_SetItemString(init_params, "kernel", PyUnicode_FromString("rbf"));
    PyDict_SetItemString(init_params, "probability", Py_True);

    // Declare the model
    PyObject * model = PyObject_Call(SVC, EARG, init_params);

    // Import model parameters
    PyObject * fit = PyObject_GetAttrString(model, "fit");
    PyObject * predict = PyObject_GetAttrString(model, "predict");
    PyObject * predict_prob = PyObject_GetAttrString(model, "predict_proba");

    PyObject * fit_args = PyTuple_New(2);
    PyObject * pred_args = PyTuple_New(1);
    
    while(true){
        if(prices.size() >= start_limit){
            // Makes sure old data is not included
            Inputs.clear();
            outputs.clear();
            NInputs.clear();

            // Compute the dataframe and normalize the data
            DataFrame(prices, std::ref(Inputs), std::ref(outputs));
            Normalize(Inputs, std::ref(NInputs));

            // Split data into training and testing with the last element of the input vector being the predictor
            Train = {NInputs.begin(), NInputs.end() - 1};
            Test = {NInputs.end() - 1, NInputs.end()};

            PyTuple_SetItem(fit_args, 0, list2D(Train));
            PyTuple_SetItem(fit_args, 1, list1D(outputs));

            PyObject_CallObject(fit, fit_args);

            PyTuple_SetItem(pred_args, 0, list2D(Test));

            // Generate the predictions and probabilities and parse them back into C++ vectors
            PyObject * pred_result = PyObject_CallObject(predict, pred_args);
            PyObject * prob_result = PyObject_CallObject(predict_prob, pred_args);
            Py_ssize_t size1 = PySequence_Size(pred_result);
            Py_ssize_t size2 = PySequence_Size(prob_result);
    
            std::vector<double> OUTPUT;
            for(Py_ssize_t i = 0; i < size1; ++i){
                OUTPUT.push_back(PyFloat_AsDouble(PySequence_GetItem(pred_result, i)));  
            }
            for(Py_ssize_t i = 0; i < size2; ++i){
                PyObject * temporary = PySequence_GetItem(prob_result, i);
                for(Py_ssize_t j = 0; j < PySequence_Size(temporary); ++j){
                    OUTPUT.push_back(PyFloat_AsDouble(PySequence_GetItem(temporary, j)));
                }
            }

            // Classification of going long or short with the given probability
            if(OUTPUT[0] == 0){
                std::cout << "The chance of a Long working is " << OUTPUT[1] << std::endl;
            } else {
                std::cout << "The chance of a Short working is " << OUTPUT[2] << std::endl;
            }
            

            std::this_thread::sleep_for(std::chrono::seconds(3));

        } else {
            std::cout << "Prices left to load: " << start_limit - prices.size() << std::endl;
        }
    }

    // End of the Python Header
    Py_Finalize();

    return 0;
}
