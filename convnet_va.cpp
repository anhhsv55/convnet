#include <cstdlib>
#include <exception>
#include <assert.h>
#include <sstream>
#include <fstream>
#include <iostream>
#include <cstdint>
#include <string>
#include <cmath>
#include <vector>
#include <ctime>
#include <boost/random.hpp>

float_t ALPHA = 0.05;
float_t LAMBDA = 0.01;

using namespace std;

inline int uniform_rand(int min, int max) {
  static boost::mt19937 gen(0);
  boost::uniform_smallint<> dst(min, max);
  return dst(gen);
}

template<typename T>
inline T uniform_rand(T min, T max) {
  static boost::mt19937 gen(0);
  boost::uniform_real<T> dst(min, max);
  return dst(gen);
}

template<typename Iter>
void uniform_rand(Iter begin, Iter end, float_t min, float_t max) {
  for (Iter it = begin; it != end; ++it)
    *it = uniform_rand(min, max);
}

class Layer {
  public:
    Layer(int depth, int height, int width, int spatialExtent, int stride,
        int zeroPadding, float_t alpha, float_t lambda, Layer *prev) {
      _depth = depth;
      _height = height;
      _width = width;
      _spatialExtent = spatialExtent;
      _stride = stride;
      _zeroPadding = zeroPadding;
      _alpha = alpha;
      _lambda = lambda;
      _prev = prev;
      cout << depth << ' ' << height << ' ' << width << ' ' << spatialExtent << ' ' << stride << ' ' << zeroPadding << ' ' << alpha << ' ' << lambda << endl;
      _output.resize(depth * width * height);
    }
    virtual void feedForward() = 0;
    virtual void backProp(const vector<float_t> &nextErrors) = 0;
    
    int _width, _height, _depth, _spatialExtent, _stride, _zeroPadding;
    float_t _alpha, _lambda;
    vector<float_t> _output;
    Layer *_prev;
    vector<float_t> _errors;

  protected:
    vector<float_t> _weight;
    vector<float_t> _bias;
    vector<float_t> _deltaW;
    int getIndex(int d, int h, int w) {
      return d * (_height * _width) + h * _width + w;
    }
    float_t activationFunction(float_t v) {
      return 1.0 / (1.0 + exp(-v));
    }
    float_t activationDerivativeFunction(float_t v) {
      return v * (1.0 - v);
    }
};

class Input: public Layer {
  public:
    Input(int depth, int height, int width): Layer(depth, height, width, 0, 0, 0, 0, 0, NULL) {}
    void setOutput(const vector<float_t> &output) {
      _output = output;
      //cout << output.size() << ' ' << _height << ' ' << _width << ' ' << endl;
      //for (int i = 0; i < _height; i++) {
        //for (int j = 0; j < _width; j++) cout << output[i * _width + j] << ' ';
        //cout << endl;
      //}
    }
    void feedForward(){}
    void backProp(const vector<float_t> &nextErrors){}
};

class ConvolutionalLayer: public Layer {
  public:
    ConvolutionalLayer(int depth, int spatialExtent, int stride, int zeroPadding, Layer *prev):
      Layer(depth, (prev->_height - spatialExtent + 2 * zeroPadding)/stride + 1,
            (prev->_width - spatialExtent + 2 * zeroPadding)/stride + 1,
            spatialExtent, stride, zeroPadding, ALPHA, LAMBDA, prev) {
      
     _weight.resize(spatialExtent * spatialExtent * prev->_depth * _depth);
     _deltaW.resize(spatialExtent * spatialExtent * prev->_depth * _depth);
     _bias.resize(_depth * _height * _width);
     initWeight();
    }

    void feedForward() {
      // CPU feedforward
      for (int out = 0; out < _depth; out++) {
        for (int h = 0; h < _height; h++) {
          for (int w = 0; w < _width; w++) {
            float_t result = 0;
            for (int in = 0; in < _prev->_depth; in++) {
              result += sumWeight(in, out, h, w);
            }
            int index = getIndex(out, h, w);
            _output[index] = activationFunction(result + _bias[index]);
          } 
        }
      }
    }

    void backProp(const vector<float_t> &nextErrors) {
      int inWidth = _prev->_width, inHeight = _prev->_height, inDepth = _prev->_depth;
      int F = _spatialExtent;

      _errors.clear();
     
      _errors.resize(inWidth * inHeight * inDepth);

      for (int out = 0; out < _depth; out++) {
        for (int h = 0; h < _height; h++) {
          for (int w = 0; w < _width; w++) {
            int inH = h * _stride;
            int inW = w * _stride;
            for (int in = 0; in < inDepth; in++) {
              for (int y = 0; y < _spatialExtent; y++) {
                for (int x = 0; x < _spatialExtent; x++) {
                  int index = in * inWidth * inHeight + (h + y) * inWidth + (x + w);
                  //int weightIndex = in * _depth * F * F + out * F * F + (F - 1 - y) * F + (F - 1 - x);
                  int weightIndex = in * _depth * F * F + out * F * F + y * F + x;
                  _errors[index] += nextErrors[out * _height * _width + h * _width + w]
                  * _weight[weightIndex] * activationDerivativeFunction(_prev->_output[index]);
                }
              }
            }
          }
        }
      }
      for (int i = 0; i < 10; i++) {
        //printf("Error convo %d %.9lf\n", i, _errors[i]);
      }
      // update weight
      for (int out = 0; out < _depth; out++) {
        for (int h = 0; h < _height; h++) {
          for (int w = 0; w < _width; w++) {
            int outIndex = out * _width * _height + h * _width + w;
            for (int in = 0; in < inDepth; in++) {
              for (int y = 0; y < F; y++) {
                for (int x = 0; x < F; x++) {
                  //int target = in * _depth * F * F + out * F * F + (F - y - 1) * F + (F - x - 1);
                  int target = in * _depth * F * F + out * F * F + y * F + x;
                  int inH = h * _stride + y;
                  int inW = w * _stride + x;
                  float_t input = _prev->_output[in * inHeight * inWidth + inH * inWidth + inW];

                  float_t delta = _alpha * input * nextErrors[outIndex] + _lambda * _deltaW[target];
                  _weight[target] -= delta;
                  // update momentum
                  _deltaW[target] = delta;
                }
              }
              _bias[outIndex] -= _alpha * nextErrors[outIndex];
            }
          }
        }
      }
    }

    void initWeight() {
      uniform_rand(_weight.begin(), _weight.end(), -1, 1);
      uniform_rand(_bias.begin(), _bias.end(), -1, 1);
      cout << _weight[1] << ' ' << _bias[1] << ' ' << _weight.size() + _bias.size() << endl;
    }

  private:
    vector<float_t> _weight;
    vector<float_t> _bias;
    float_t sumWeight(int in, int out, int h, int w) {
      int startH = h * _stride;
      int startW = w * _stride;
      int inHeight = _prev->_height;
      int inWidth = _prev->_width;
      float_t result = 0;
      int F = _spatialExtent;
      for (int i = 0; i < F; i++) {
        for (int j = 0; j < F; j++) {
          int index = in * (inHeight * inWidth) + (startH + i) * inWidth + (startW + j); // row startH + i, col startW + j
          float_t input = _prev->_output[index];
          int inDepth = _prev->_depth;
          int indexWeight = in * _depth * F * F + out * F * F + i * F + j;
          //int indexWeight = in * _depth * F * F + out * F * F + (F - i -1) * F + (F - 1 - j);
          //if (getIndex(out, h, w) == 69) {
            //cout << in << ' ' << startH + i << ' ' << startW + j << endl;
          //}
          result += input * _weight[indexWeight];
        }
      }
      //if (getIndex(out, h, w) == 69) {
        //for (int i = 0; i < x.size(); i++) cout << x[i] << ' ';cout << endl;
        //for (int i = 0; i < y.size(); i++) cout << y[i] << ' ';cout << endl;
      //}
      return result;
    }
};

class MaxPoolingLayer: public Layer {
  public:
    MaxPoolingLayer(int spatialExtent, int stride, Layer *prev):
      Layer(prev->_depth, (prev->_height - spatialExtent)/stride + 1,
          (prev->_width - spatialExtent)/stride + 1,
          spatialExtent, stride, 0, 0, 0, prev) {

        _maxIndex.resize(_depth * _height * _width);
      }
    void feedForward() {
      for (int d = 0; d < _depth; d++) {
        for (int h = 0; h < _height; h++) {
          for (int w = 0; w < _width; w++) {
            int index = getIndex(d, h, w);
            _output[index] = getMax(d, h, w, index);
          }
        }
      }
    }
    void backProp(const vector<float_t> &nextErrors) {
      _errors.clear();
      _errors.resize(_prev->_depth * _prev->_height * _prev->_width);    
      for (int i = 0; i < _maxIndex.size(); i++) {
        _errors[_maxIndex[i]] = nextErrors[i];
      } 
      for (int i = 0; i < 10; i++) {
        //printf("Error pooling %d %.9f\n", i, _errors[i]);
      }
    }

    void initWeight() {}
    private:
      vector<int> _maxIndex;
      float_t getMax(int d, int h, int w, int outIndex) {
        int startH = h * _stride;
        int startW = w * _stride;
        int H = _prev->_height;
        int W = _prev->_width;
        float_t result = -1000000000;
        for (int i = startH; i < startH + _spatialExtent; i++) {
          for (int j = startW; j < startW + _spatialExtent; j++) {
            int index = d * (H * W) + i * W + j;
            if (_prev->_output[index] > result) {
              result = _prev->_output[index]; 
              _maxIndex[outIndex] = index;
            }
          }
        }
        return result;
      }
};

class FullyConnectedLayer: public Layer {
  public:
    FullyConnectedLayer(int depth, Layer *prev): Layer(depth, 1, 1, 0, 0, 0, ALPHA, LAMBDA, prev) {
      _weight.resize(depth * prev->_depth);
      _bias.resize(depth);
      _deltaW.resize(depth * prev->_depth);
      initWeight();
    }

    void feedForward() {
      int inDepth = _prev->_depth;
      for (int out = 0; out < _depth; out++) {
        float_t result = 0;
        for (int in = 0; in < inDepth; in++) {
          result += _weight[out * inDepth + in] * _prev->_output[in];
        }
        _output[out] = activationFunction(result + _bias[out]);
      }
    }

    void backProp(const vector<float_t> &nextErrors) {
      // calculate the error term
      // equal to (next layer error term) x (transpose of weight matrix to next layer) * (activationDerivative of input)
      
      _errors.clear();
      _errors.resize(_prev->_depth);
      int inDepth = _prev->_depth;
      for (int in = 0; in < inDepth; in++) {
        float_t result = 0;
        for (int out = 0; out < _depth; out++) {
          result += nextErrors[out] * _weight[inDepth * out + in];
        }
        _errors[in] = result * activationDerivativeFunction(_prev->_output[in]);
      }

      for (int out = 0; out < _depth; out++) {
        for (int in = 0; in < inDepth; in++) {
          // learning rate * 
          int index = out * inDepth + in;
          float_t delta = _alpha * _prev->_output[in] * nextErrors[out] + _lambda * _deltaW[index];
          _weight[index] -= delta;
          _deltaW[index] = delta;
        }
        _bias[out] -= _alpha * nextErrors[out];
      }
      //for (int i = 0; i < inDepth; i++) printf("%.9f ", _errors[i]);
      //cout << endl;
    }

    void initWeight() {
			uniform_rand(_weight.begin(), _weight.end(), -2, 2);
			uniform_rand(_bias.begin(), _bias.end(), -2, 2);
      cout << _weight[1] << ' ' << _bias[1] << ' ' << _weight.size() + _bias.size() << endl;
    }
};

class OutputLayer: public Layer {
  public:
    OutputLayer(Layer *prev): Layer(prev->_depth, 1, 1, 0, 0, 0, 0, 0, prev) { }

    void setLabel(int label) {
      _label = label;
    }

    void feedForward() {
      _output = _prev->_output;
    }

    float_t getError() {
      float_t err = 0;
      for (int i = 0; i < _depth; i++) {
        int expected = (i == _label) ? 1 : 0;
        err += 0.5 * (_output[i] - expected) * (_output[i] - expected);
        //cout << _output[i] << ' ';
      }
      //cout << endl;
      return err;
    }

    int getPredict() {
      int index = 0;
      for (int i = 1; i < _depth; i++) if (_output[i] > _output[index]) index = i;
      //for (int i = 0; i < _depth; i++) cout << _output[i] << ' ';
      return index;
    }

    void backProp(const vector<float_t> &nextErrors) {
      _errors.clear();
      _errors.resize(_depth);
      for (int i = 0; i < _depth; i++) {
        int expected = (i == _label) ? 1 : 0;
        _errors[i] = (_output[i] - expected) * activationDerivativeFunction(_prev->_output[i]);
      }
      //for (int i = 0; i < _errors.size(); i++) cout << _errors[i] << ' ';
      //cout << endl;
    }

    int _label;
};

  struct Image {
    vector< vector<float_t> > img;// a image is represented by a 2-dimension vector  
    size_t size; // width or height

    // construction
    Image(size_t size_, vector< vector<float_t> > img_) :img(img_), size(size_){}

    // display the image
    void display(){
      for (size_t i = 0; i < size; i++){
        for (size_t j = 0; j < size; j++){
          if (img[i][j] > 200)
            cout << 1;
          else
            cout << 0;
        }
        cout << endl;
      }
    }

    // up size to 32, make up with 0
    void upto_32(){
      assert(size < 32);

      vector<float_t> row(32, 0);

      for (size_t i = 0; i < size; i++){
        img[i].insert(img[i].begin(), 0);
        img[i].insert(img[i].begin(), 0);
        img[i].push_back(0);
        img[i].push_back(0);
      }
      img.insert(img.begin(), row);
      img.insert(img.begin(), row);
      img.push_back(row);
      img.push_back(row);

      size = 32;
    }

    vector<float_t> extend(){
      vector<float_t> v;
      for (size_t i = 0; i < size; i++){
        for (size_t j = 0; j < size; j++){
          v.push_back(img[i][j]);
        }
      }
      return v;
    }
  };

  typedef Image* Img;

  struct Sample
  {
    uint8_t label; // label for a specific digit
    vector<float_t> image;
    Sample(float_t label_, vector<float_t> image_) :label(label_), image(image_){}
  };

class Mnist_Parser
  {
  public:
    Mnist_Parser(string data_path) :
      test_img_fname(data_path + "/t10k-images-idx3-ubyte"),
      test_lbl_fname(data_path + "/t10k-labels-idx1-ubyte"),
      train_img_fname(data_path + "/train-images-idx3-ubyte"),
      train_lbl_fname(data_path + "/train-labels-idx1-ubyte"){}

    vector<Sample*> load_testing(){
      test_sample = load(test_img_fname, test_lbl_fname);
      return test_sample;
    }

    vector<Sample*> load_training(){
      train_sample = load(train_img_fname, train_lbl_fname);
      return train_sample;
    }

    void test(){
      srand((int)time(0));
      size_t i = (int)(rand());
      cout << i << endl;
      cout << (int)test_sample[i]->label << endl;
      //test_sample[i]->image->display();

      size_t j = (int)(rand() * 60000);
      cout << (int)(train_sample[i]->label) << endl;
      //train_sample[i]->image->display();

    }

    // vector for store test and train samples
    vector<Sample*> test_sample;
    vector<Sample*> train_sample;

  private:
    vector<Sample*> load(string fimage, string flabel){
      ifstream in;
      in.open(fimage, ios::binary | ios::in);
      if (!in.is_open()){
        cout << "file opened failed." << endl;
      }

      uint32_t magic = 0;
      uint32_t number = 0;
      uint32_t rows = 0;
      uint32_t cols = 0;

      in.read((char*)&magic, sizeof(uint32_t));
      in.read((char*)&number, sizeof(uint32_t));
      in.read((char*)&rows, sizeof(uint32_t));
      in.read((char*)&cols, sizeof(uint32_t));

      assert(swapEndien_32(magic) == 2051);
      cout << "number:" << swapEndien_32(number) << endl;
      assert(swapEndien_32(rows) == 28);
      assert(swapEndien_32(cols) == 28);

      vector< float_t> row;
      vector< vector<float_t> > img;
      vector<Img> images;

      uint8_t pixel = 0;
      size_t col_index = 0;
      size_t row_index = 0;
      while (!in.eof()){
        in.read((char*)&pixel, sizeof(uint8_t));
        col_index++;
        row.push_back((float_t)pixel);
        if (col_index == 28){
          img.push_back(row);

          row.clear();
          col_index = 0;

          row_index++;
          if (row_index == 28){
            Img i = new Image(28, img);
            i->upto_32();
            //i->display();
            images.push_back(i);
            img.clear();
            row_index = 0;
          }
        }
      }

      in.close();

      assert(images.size() == swapEndien_32(number));

      //label
      in.open(flabel, ios::binary | ios::in);
      if (!in.is_open()){
        cout << "failed opened label file";
      }

      in.read((char*)&magic, sizeof(uint32_t));
      in.read((char*)&number, sizeof(uint32_t));

      assert(2049 == swapEndien_32(magic));
      assert(swapEndien_32(number) == images.size());

      vector<uint8_t>  labels;

      uint8_t label;
      while (!in.eof())
      {
        in.read((char*)&label, sizeof(uint8_t));
        //cout << (int)label << endl;
        labels.push_back(label);
      }

      vector<Sample*> samples;
      for (int i = 0; i < swapEndien_32(number); i++){
        samples.push_back(new Sample(labels[i], images[i]->extend()));
      }

      cout << "Loading complete" << endl;
      in.close();
      return samples;
    }

    // reverse endien for uint32_t
    uint32_t swapEndien_32(uint32_t value){
      return ((value & 0x000000FF) << 24) |
        ((value & 0x0000FF00) << 8) |
        ((value & 0x00FF0000) >> 8) |
        ((value & 0xFF000000) >> 24);
    }

    // filename for mnist data set
    string test_img_fname;
    string test_lbl_fname;
    string train_img_fname;
    string train_lbl_fname;
  };

void initializeNet(vector<Layer*> &layers) {
  // Convolutional - Depth, spatialExtent, stride, zeroPadding
  // MaxPooling - SpatialExtent, stride
  // FullyConnected - Depth
  layers.push_back(new Input(1, 32, 32));
  layers.push_back(new ConvolutionalLayer(6, 5, 1, 0, layers.back())); // => 6 * 28 * 28
  layers.push_back(new MaxPoolingLayer(2, 2, layers.back())); // => 6 * 14 * 14 
  layers.push_back(new ConvolutionalLayer(16, 5, 1, 0, layers.back())); // => 16 * 10 * 10
  layers.push_back(new MaxPoolingLayer(2, 2, layers.back())); // => 16 * 5 * 5
  layers.push_back(new ConvolutionalLayer(100, 5, 1, 0, layers.back())); // => 100 * 1 * 1
  layers.push_back(new FullyConnectedLayer(10, layers.back())); // => 10 * 1 * 1
  layers.push_back(new OutputLayer(layers.back()));
}

void train(vector<Layer*> layers) {
  Mnist_Parser parser(".");
  auto input = parser.load_training();  
  for (int test = 0; test < 60000/* input.size())*/; test++) {
     
    auto i = test % 60000;
    if (test % 100 == 0) {
      cout << test << endl;
    }
    //int i = test;
    ((Input*)layers[0])->setOutput(input[i]->image);
    ((OutputLayer*)layers.back())->setLabel(input[i]->label);
    //cout << "Label = " << ' ' << i << ' ' << (int)input[i]->label << endl;
    //cout << test << ' ' << i << endl;
    int iter = 0;
    float_t errors = 0;
    int M = 1;
    do {
      for (int l = 0; l < layers.size(); l++) {
        layers[l]->feedForward();
      }

      vector<float_t> nextErrors;
      for (int l = layers.size() - 1; l >= 0; l--) {
        layers[l]->backProp(nextErrors);
        nextErrors = layers[l]->_errors;
      }
      float_t x = ((OutputLayer*)layers.back())->getError();
      errors += x;
      iter++;
    } while (errors/M > 1e-3 && iter < M);
    //cout << test << ' ' << errors/M << endl;
  }
  auto testInput = parser.load_testing();
  int correct = 0;
  for (int i = 0; i < testInput.size(); i++) {
    ((Input*)layers[0])->setOutput(testInput[i]->image);
    ((OutputLayer*)layers.back())->setLabel(testInput[i]->label);
    for (int l = 0; l < layers.size(); l++) {
      layers[l]->feedForward();
    }
    correct += ((OutputLayer*)layers.back())->_label == ((OutputLayer*)layers.back())->getPredict();
    //cout << ((OutputLayer*)layers.back())->_label << ' ' << ((OutputLayer*)layers.back())->getPredict() << endl;
  }
  cout << correct << ' ' << testInput.size() << endl;
}

int main() {
  cout << ALPHA << ' ' << LAMBDA << endl;
  srand(time(NULL));
  vector<Layer*> layers;
  initializeNet(layers);
  train(layers);
  return 0;
}
