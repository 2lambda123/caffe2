#ifndef CAFFE2_OPERATORS_LSTM_UNIT_OP_H_
#define CAFFE2_OPERATORS_LSTM_UNIT_OP_H_

#include "caffe2/core/context.h"
#include "caffe2/core/operator.h"

namespace caffe2 {
namespace detail {

template <typename T>
inline T sigmoid(T x) {
  return 1. / (1. + exp(-x));
}

template <typename T>
inline T tanh(T x) {
  return 2. * sigmoid(2. * x) - 1.;
}

template <typename T, typename Context>
void LSTMUnit(
    int N,
    int D,
    int t,
    const T* C_prev,
    const T* X,
    const int32_t* seqLengths,
    T* C,
    T* H,
    Context* context) {
  for (int n = 0; n < N; ++n) {
    const bool valid = t < seqLengths[n];
    for (int d = 0; d < D; ++d) {
      if (!valid) {
        H[d] = 0;
        C[d] = C_prev[d];
      } else {
        const T i = sigmoid(X[d]);
        const T f = sigmoid(X[1 * D + d]);
        const T o = sigmoid(X[2 * D + d]);
        const T g = tanh(X[3 * D + d]);
        const T c_prev = C_prev[d];
        const T c = f * c_prev + i * g;
        C[d] = c;
        const T tanh_c = tanh(c);
        H[d] = o * tanh_c;
      }
    }
    C_prev += D;
    X += 4 * D;
    C += D;
    H += D;
  }
}

template <typename T, typename Context>
void LSTMUnitGradient(
    int N,
    int D,
    int t,
    const T* C_prev,
    const T* X,
    const int32_t* seqLengths,
    const T* C,
    const T* H,
    const T* C_diff,
    const T* H_diff,
    T* C_prev_diff,
    T* X_diff,
    Context* context) {
  for (int n = 0; n < N; ++n) {
    const bool valid = t < seqLengths[n];
    for (int d = 0; d < D; ++d) {
      T* c_prev_diff = C_prev_diff + d;
      T* i_diff = X_diff + d;
      T* f_diff = X_diff + 1 * D + d;
      T* o_diff = X_diff + 2 * D + d;
      T* g_diff = X_diff + 3 * D + d;
      if (!valid) {
        *c_prev_diff = C_diff[d];
        *i_diff = 0;
        *f_diff = 0;
        *o_diff = 0;
        *g_diff = 0;
      } else {
        const T i = sigmoid(X[d]);
        const T f = sigmoid(X[1 * D + d]);
        const T o = sigmoid(X[2 * D + d]);
        const T g = tanh(X[3 * D + d]);
        const T c_prev = C_prev[d];
        const T c = C[d];
        const T tanh_c = tanh(c);
        const T c_term_diff = C_diff[d] + H_diff[d] * o * (1 - tanh_c * tanh_c);
        *c_prev_diff = c_term_diff * f;
        *i_diff = c_term_diff * g * i * (1 - i);
        *f_diff = c_term_diff * c_prev * f * (1 - f);
        *o_diff = H_diff[d] * tanh_c * o * (1 - o);
        *g_diff = c_term_diff * i * (1 - g * g);
      }
    }
    C_prev += D;
    X += 4 * D;
    C += D;
    H += D;
    C_diff += D;
    H_diff += D;
    X_diff += 4 * D;
    C_prev_diff += D;
  }
}
} // namespace detail

template <typename T, typename Context>
class LSTMUnitOp : public Operator<Context> {
 public:
  USE_OPERATOR_CONTEXT_FUNCTIONS;
  using Operator<Context>::Operator;

  bool RunOnDevice() override {
    // Extract N
    const auto N = Input(CELL_T_M_1).dim(1);
    // Gates: 1xNxG
    const auto G = Input(GATES).dim(2);
    const auto D = Input(CELL_T_M_1).dim(2);
    CHECK_EQ(4 * D, G);
    const auto* C_prev = Input(CELL_T_M_1).template data<T>();
    const auto* X = Input(GATES).template data<T>();
    const auto* seqLengths = Input(SEQ_LENGTHS).template data<int32_t>();
    const auto t = OperatorBase::Input<Tensor<CPUContext>>(TIMESTEP)
                       .template data<int32_t>()[0];
    Output(CELL_T)->ResizeLike(Input(CELL_T_M_1));
    auto* C = Output(CELL_T)->template mutable_data<T>();
    Output(HIDDEN_T)->ResizeLike(Input(CELL_T_M_1));
    auto* H = Output(HIDDEN_T)->template mutable_data<T>();
    detail::LSTMUnit<T, Context>(
        N, D, t, C_prev, X, seqLengths, C, H, &context_);
    return true;
  }

 protected:
  INPUT_TAGS(CELL_T_M_1, GATES, SEQ_LENGTHS, TIMESTEP);
  OUTPUT_TAGS(HIDDEN_T, CELL_T);
};

template <typename T, typename Context>
class LSTMUnitGradientOp : public Operator<Context> {
 public:
  USE_OPERATOR_CONTEXT_FUNCTIONS;
  using Operator<Context>::Operator;

  bool RunOnDevice() override {
    // Extract N
    const auto N = Input(CELL_T_M_1).dim(1);
    // Gates: 1xNxG
    const auto G = Input(GATES).dim(2);
    const auto D = Input(CELL_T_M_1).dim(2);
    CHECK_EQ(4 * D, G);
    const auto* C_prev = Input(CELL_T_M_1).template data<T>();
    const auto* X = Input(GATES).template data<T>();
    const auto t = OperatorBase::Input<Tensor<CPUContext>>(TIMESTEP)
                       .template data<int32_t>()[0];
    const auto* C = Input(CELL_T).template data<T>();
    const auto* H = Input(HIDDEN_T).template data<T>();
    const auto* C_diff = Input(CELL_T_GRAD).template data<T>();
    const auto* H_diff = Input(HIDDEN_T_GRAD).template data<T>();
    const auto* seqLengths = Input(SEQ_LENGTHS).template data<int32_t>();
    Output(CELL_T_M_1_GRAD)->ResizeLike(Input(CELL_T_M_1));
    auto* C_prev_diff = Output(CELL_T_M_1_GRAD)->template mutable_data<T>();
    Output(GATES_GRAD)->ResizeLike(Input(GATES));
    auto* X_diff = Output(GATES_GRAD)->template mutable_data<T>();

    detail::LSTMUnitGradient<T, Context>(
        N,
        D,
        t,
        C_prev,
        X,
        seqLengths,
        C,
        H,
        C_diff,
        H_diff,
        C_prev_diff,
        X_diff,
        &context_);
    return true;
  }

 protected:
  INPUT_TAGS(
      CELL_T_M_1,
      GATES,
      SEQ_LENGTHS,
      TIMESTEP,
      HIDDEN_T,
      CELL_T,
      HIDDEN_T_GRAD,
      CELL_T_GRAD, );
  OUTPUT_TAGS(CELL_T_M_1_GRAD, GATES_GRAD);
};

} // namespace caffe2

#endif // CAFFE2_OPERATORS_LSTM_UNIT_OP_H_
