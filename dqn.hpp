#ifndef DQN_HPP_
#define DQN_HPP_

#include <memory>
#include <random>
#include <tuple>
#include <unordered_map>
#include <vector>
#include <ale_interface.hpp>
#include <caffe/caffe.hpp>
#include <boost/functional/hash.hpp>
#include <boost/optional.hpp>

namespace dqn {

constexpr auto kRawFrameHeight         = 210;
constexpr auto kRawFrameWidth          = 160;
constexpr auto kCroppedFrameSize       = 84;
constexpr auto kCroppedFrameDataSize   = kCroppedFrameSize * kCroppedFrameSize;
constexpr auto kOutputCount            = 18;

constexpr auto frames_layer_name = "frames_input_layer";
constexpr auto cont_layer_name   = "cont_input_layer";
constexpr auto target_layer_name = "target_input_layer";
constexpr auto filter_layer_name = "filter_input_layer";

constexpr auto train_frames_blob_name = "frames";
constexpr auto test_frames_blob_name  = "all_frames";
constexpr auto target_blob_name       = "target";
constexpr auto filter_blob_name       = "filter";
constexpr auto cont_blob_name         = "cont";
constexpr auto q_values_blob_name     = "q_values";

constexpr auto ip1Size = 512;
constexpr auto lstmSize = 512;

using FrameData        = std::array<uint8_t, kCroppedFrameDataSize>;
using FrameDataSp      = std::shared_ptr<FrameData>;
using InputFrames      = std::vector<FrameDataSp>;
using InputFramesBatch = std::vector<InputFrames>;
using Transition       = std::tuple<FrameDataSp, Action, float,
                                boost::optional<FrameDataSp> >;
using Episode          = std::vector<Transition>;
using ReplayMemory     = std::deque<Episode>;
using MemoryLayer      = caffe::MemoryDataLayer<float>;
using FrameVec         = std::vector<FrameDataSp>;

using ActionValue = std::pair<Action, float>;
using SolverSp = std::shared_ptr<caffe::Solver<float>>;
using NetSp = boost::shared_ptr<caffe::Net<float>>;

/**
 * Deep Q-Network
 */
class DQN {
public:
  DQN(const ActionVect& legal_actions,
      const int replay_memory_capacity,
      const double gamma,
      const int clone_frequency,
      const int unroll,
      const int minibatch_size,
      const int input_frames_per_timestep);

  // Initialize DQN. Must be called before calling any other method.
  void Initialize(caffe::SolverParameter& solver_param);

  // Create the caffe net .prototxt
  caffe::NetParameter CreateNet(bool unroll1_is_lstm);

  // Load a trained model from a file.
  void LoadTrainedModel(const std::string& model_file);

  // Restore solving from a solver file.
  void RestoreSolver(const std::string& solver_file);

  // Snapshot the model/solver/replay memory. Produces files:
  // snapshot_prefix_iter_N.[caffemodel|solverstate|replaymem]. Optionally
  // removes snapshots that share the same prefix but have a lower
  // iteration number.
  void Snapshot(const string& snapshot_prefix, bool remove_old=false,
                bool snapshot_memory=true);

  // Select an action by epsilon-greedy. If cont is false, LSTM state
  // will be reset. cont should be true only at start of new episodes.
  Action SelectAction(const InputFrames& frames, double epsilon, bool cont);

  // Select a batch of actions by epsilon-greedy.
  ActionVect SelectActions(const InputFramesBatch& frames_batch,
                           double epsilon, bool cont);

  // Add an episode to the replay memory
  void RememberEpisode(const Episode& episode);

  // Update DQN. Returns the number of solver steps executed.
  int UpdateSequential();
  // Updates from a random minibatch of experiences
  int UpdateRandom();

  // Clear the replay memory
  void ClearReplayMemory();

  // Save the replay memory to a gzipped compressed file
  void SnapshotReplayMemory(const std::string& filename);

  // Load the replay memory from a gzipped compressed file
  void LoadReplayMemory(const std::string& filename);

  // Get the number of episodes stored in the replay memory
  int memory_episodes() const { return replay_memory_.size(); }

  // Get the number of transitions store in the replay memory
  int memory_size() const { return replay_memory_size_; }

  // Return the current iteration of the solver
  int current_iteration() const { return solver_->iter(); }

  void CloneTestNet() { CloneNet(*test_net_); }

  void Benchmark(int iterations=1000);

protected:
  // Clone the given net and store the result in clone_net_
  void CloneNet(caffe::Net<float>& net);

  // Given a set of input frames and a network, select an
  // action. Returns the action and the estimated Q-Value.
  ActionValue SelectActionGreedily(caffe::Net<float>& net,
                                   const InputFrames& frames,
                                   bool cont);

  // Given a vector of frames, return a batch of selected actions + values.
  std::vector<ActionValue> SelectActionGreedily(
      caffe::Net<float>& net, const std::vector<InputFrames>& frames, bool cont);

  // Input data into the Frames/Target/Filter layers of the given
  // net. This must be done before forward is called.
  void InputDataIntoLayers(caffe::Net<float>& net,
                           float* frames_input,
                           float* cont_input,
                           float* target_input,
                           float* filter_input);

protected:
  int unroll_; // Number of steps to unroll recurrent layers
  int minibatch_size_; // Size of each minibatch
  int frames_per_timestep_; // History of frames given at each timestep
  int frames_per_forward_; // Number of frames needed by each forward

  std::vector<float> frame_input_;
  std::vector<float> target_input_;
  std::vector<float> filter_input_;
  std::vector<float> cont_input_;

  const ActionVect legal_actions_;
  const int replay_memory_capacity_;
  const double gamma_;
  const int clone_frequency_; // How often (steps) the clone_net is updated
  int replay_memory_size_; // Number of transitions in replay memory
  ReplayMemory replay_memory_;
  SolverSp solver_;
  NetSp net_; // The primary network used for action selection.
  NetSp test_net_; // Net used for testing
  NetSp clone_net_; // Clone used to generate targets.
  int last_clone_iter_; // Iteration in which the net was last cloned
  std::mt19937 random_engine;
};

/**
 * Returns a vector of filenames matching a given regular expression.
 */
std::vector<std::string> FilesMatchingRegexp(const std::string& regexp);

/**
 * Removes snapshots starting with snapshot_prefix that have an
 * iteration less than min_iter.
 */
void RemoveSnapshots(const std::string& snapshot_prefix, int min_iter);

/**
 * Look for the latest snapshot to resume from. Returns a string
 * containing the path to the .solverstate. Returns empty string if
 * none is found. Will only return if the snapshot contains all of:
 * .solverstate,.caffemodel,.replaymemory
 */
std::string FindLatestSnapshot(const std::string& snapshot_prefix);

/**
 * Preprocess an ALE screen (downsampling & grayscaling)
 */
FrameDataSp PreprocessScreen(const ALEScreen& raw_screen);

}

#endif /* DQN_HPP_ */
