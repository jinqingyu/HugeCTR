from hugectr import Session, solver_parser_helper, get_learning_rate_scheduler
import sys
from mpi4py import MPI
def model_oversubscriber_test(json_file, temp_dir):
  dataset = [("file_list."+str(i)+".txt", "file_list."+str(i)+".keyset") for i in range(5)]
  solver_config = solver_parser_helper(seed = 0,
                                     batchsize = 16384,
                                     batchsize_eval =16384,
                                     model_file = "",
                                     embedding_files = [],
                                     vvgpu = [[0]],
                                     use_mixed_precision = False,
                                     scaler = 1.0,
                                     i64_input_key = False,
                                     use_algorithm_search = True,
                                     use_cuda_graph = True,
                                     repeat_dataset = False
                                    )
  lr_sch = get_learning_rate_scheduler(json_file)
  sess = Session(solver_config, json_file, True, temp_dir)
  data_reader_train = sess.get_data_reader_train()
  data_reader_eval = sess.get_data_reader_eval()
  data_reader_eval.set_source("file_list.5.txt")
  model_oversubscriber = sess.get_model_oversubscriber()
  iteration = 0
  for file_list, keyset_file in dataset:
    data_reader_train.set_source(file_list)
    model_oversubscriber.update(keyset_file)
    while True:
      lr = lr_sch.get_next()
      sess.set_learning_rate(lr)
      good = sess.train()
      if good == False:
        break
      if iteration % 100 == 0:
        sess.check_overflow()
        sess.copy_weights_for_evaluation()
        data_reader_eval = sess.get_data_reader_eval()
        good_eval = True
        j = 0
        while good_eval:
          if j >= solver_config.max_eval_batches:
            break
          good_eval = sess.eval()
          j += 1
        if good_eval == False:
          data_reader_eval.set_source()
        metrics = sess.get_eval_metrics()
        print("[HUGECTR][INFO] iter: {}, metrics: {}".format(iteration, metrics))
      iteration += 1
    print("[HUGECTR][INFO] trained with data in {}".format(file_list))
  sess.download_params_to_files("./", iteration)

if __name__ == "__main__":
  json_file = sys.argv[1]
  temp_dir = sys.argv[2]
  model_oversubscriber_test(json_file, temp_dir)
