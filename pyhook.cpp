// Python Bindings

#ifndef DISABLE_PYBIND
#include <pybind11/pybind11.h>
#include <pybind11/embed.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include <pybind11/stl_bind.h>
#include <pybind11/pytypes.h>
#include <pybind11/subinterpreter.h>
#endif

// STD libs
#include <iostream>
#include <syncstream>
#include <vector>
#include <memory>
#include <thread>
#include <unordered_map>
#include <chrono>

#ifndef DISABLE_PYBIND
namespace py = pybind11;
PYBIND11_MAKE_OPAQUE(std::vector<int>)
PYBIND11_MAKE_OPAQUE(std::vector<double>)
#endif

#include "utils.h"

using VEC_INT = std::vector<int>;
using VEC_DBL = std::vector<double>;

template <typename T>
class Thread
{
public:
   Thread():jthread_() {}
   ~Thread() = default;
   Thread(const Thread&) = delete;
   Thread(Thread&&) = delete;
   Thread operator=(const Thread&) = delete;
   Thread operator=(Thread&&) = delete;

   void run()
   {
      if (jthread_)
      {
         stop();
      }
      else
      {
         jthread_.reset(new std::jthread([&](std::stop_token stop) 
                        { static_cast<T*>(this)->runImpl(stop); }));
      }
   }

   void stop()
   {
      if (jthread_)
      {
         jthread_->request_stop();
         // maybe wait for a bit...
         //std::this_thread::sleep_for(std::chrono::seconds(3));
         jthread_->join();
         jthread_.reset();
      }
   }

   void join()
   {
      if (jthread_)
      {
         jthread_->join();
      }
   }

protected:
   std::unique_ptr<std::jthread> jthread_;
};

// Dummy producer to produce trades.
class Exchange : public Thread<Exchange>
{
public:
   Exchange(const std::string& ric, double priceLow, int volumeLow, double vola, TASK_QUEUE& queue, int max)
   : Thread<Exchange>(), 
     ric_(ric), 
     rand_price_(priceLow, priceLow*(1.0+vola)), 
     rand_volume_(volumeLow, volumeLow*(1.0+vola)), 
     queue_(queue),
     max_(max)
   {
   }

private:
   friend class Thread<Exchange>;
   void runImpl(std::stop_token stop)
   {
      for (int i = 1; i <= this->max_ && !stop.stop_requested(); ++i)
      //for (int i = 1; i <= this->max_; ++i)
      {
         TradePtr trade = Traits::create(ric_,rand_price_(),rand_volume_());
         // note in case we are using unique_ptr we lose ownership after passing it to produce
         queue_.enqueue(trade);
      }
#ifndef BENCHMARK_PYHOOK
      std::osyncstream (std::cout) << "Exchange Thread: " << ric_ << ", Total Produced: " << this->max_ << std::endl;
#endif
   }

private:
   std::string ric_;
   Rand<double> rand_price_;
   Rand<int> rand_volume_;
   TASK_QUEUE& queue_;
   int max_;
};

// Dummy consumer to consume trades.
class Strategy: public Thread<Strategy>
{
public:
   Strategy(const std::string& strategy_id, TASK_QUEUE& queue, int max)
   : Thread<Strategy>(), 
     strategy_id_(strategy_id), queue_(queue), max_(max), trades_map_()
   {
   }

private:
   friend class Thread<Strategy>;

   // Consumes trades from exchanges, then calls
   // momentum function in the python code to
   // calculate a crossing momentum signal.
   // Displays how pybind11 provides hooks into the python 
   // interpreter to call from C++.
   void runImpl(std::stop_token stop)
   {
#ifndef DISABLE_PYBIND

      // init local objects for later use can be saved as 
      // class object but  then need to be released in the 
      // destructor
      py::module_ pyModule;
      py::object func;
      {
         // get interpreter lock
         py::gil_scoped_acquire acquire;
         pyModule = py::module_::import("pyhook");
         func = pyModule.attr("momentum");
      }

#endif
      int counter = 0;
      while(!stop.stop_requested())
      //while(true)
      {
         TradePtr trade;
         if (queue_.dequeue(trade))
         {
            ++counter;

            TradeTs& tradeTs = trades_map_[trade->ric_];
            tradeTs.priceTs_.push_back(trade->price_);
            tradeTs.volumeTs_.push_back(trade->volume_);

#ifndef BENCHMARK_PYHOOK
            //std::osyncstream (std::cout) << "Strategy: " << strategy_id_
            //<< ", Ric: " << trade->ric_ << std::endl;
#endif

#ifndef DISABLE_PYBIND
            if (tradeTs.priceTs_.size() >= 20)
            {
               double priceMomentumSignal;
               double volumeMomentumSignal;
               {
                  // acquire lock
                  py::gil_scoped_acquire acquire;

                  // to pass by reference so there is no copy involved of vectors...
                  py::object result = func.operator()<py::return_value_policy::reference>(tradeTs.priceTs_);
                  priceMomentumSignal = result.cast<double>();

                  result = func.operator()<py::return_value_policy::reference>(tradeTs.volumeTs_);
                  volumeMomentumSignal = result.cast<double>();
               }

               // take some action on these signals... send orders etc...

#ifndef BENCHMARK_PYHOOK
               //std::osyncstream (std::cout) << "Strategy: " << strategy_id_ 
                  //<< ", Ric: " << trade->ric_ 
                  //<< ", PriceMomentumSignal: " << priceMomentumSignal
                  //<< ", VolumeMomentumSignal: " << volumeMomentumSignal << std::endl;
#endif
            }
#endif

            // not required if std::unique_ptr
            if constexpr(!Traits::is_unique_ptr(trade))
            {
               Traits::destroy(trade);
            }
         }
         if (counter >= this->max_)
            break;
      }

#ifndef BENCHMARK_PYHOOK
      std::osyncstream (std::cout) << "Strategy: " << strategy_id_ << ", Total Processed: " << counter << std::endl;
#endif
   }

private:

   std::string strategy_id_;
   TASK_QUEUE& queue_;
   int max_;

   struct TradeTs
   {
      VEC_DBL priceTs_;
      VEC_DBL volumeTs_;
   };   

   using TRADES_MAP = std::unordered_map<std::string, TradeTs>;

   TRADES_MAP trades_map_;
};

void run_main()
{
#ifndef DISABLE_PYBIND
   // main interpreter and inits...
   py::scoped_interpreter main_interpreter;

   py::module_ pyModule = py::module_::import("pyhook");
   py::bind_vector<VEC_DBL>(pyModule, "VectorDbl");
   py::bind_vector<VEC_INT>(pyModule, "VectorInt");

   py::gil_scoped_release release;
#endif
   {
      double vola = 0.10; // 10%

      // Set 1 Exchanges and Strategy sink
      TASK_QUEUE task_queue_1;
      Exchange exchange1 {"MSFTO.O", 490.0, 10000, vola, task_queue_1, 200};
      Exchange exchange2 {"AAPL.OQ", 230.0, 15000, vola, task_queue_1, 100};
      Strategy strategy1 {"S1", task_queue_1, 300};

      // Set 2 Exchanges and Strategy sink
      TASK_QUEUE task_queue_2;
      Exchange exchange3 {"NVDA.O" , 174.8, 20000, vola, task_queue_2, 100};
      Exchange exchange4 {"META.O" , 724.5, 21000, vola, task_queue_2, 100};
      Strategy strategy2 {"S2", task_queue_2, 200};

      exchange1.run();
      exchange2.run();
      strategy1.run();

      exchange3.run();
      exchange4.run();
      strategy2.run();

      exchange1.join();
      exchange2.join();
      strategy1.join();

      exchange3.join();
      exchange4.join();
      strategy2.join();
   }
}

#ifndef BENCHMARK_PYHOOK

int main()
{
   run_main();
   return 0;
}

#else

#include <benchmark/benchmark.h>
static void BENCHMARK_pyhook(benchmark::State& state)
{
   for (auto _ : state)
   {
      run_main();
   }
}

BENCHMARK(BENCHMARK_pyhook)->Iterations(1)->Repetitions(5)->Unit(benchmark::kMillisecond);
BENCHMARK_MAIN();

#endif
