#ifndef UTILS_INCLUDE
#define UTILS_INCLUDE

#include <iostream>
#include <vector>
#include <random>
#include "queue.h"

// forward declaration
class Exchange;
class Strategy;

// dummy trade
class Trade
{
public:
   Trade(const std::string& ric, double price, int volume)
   : ric_(ric), price_(price), volume_(volume)
   {
   }
   ~Trade() = default;
private:
   const std::string ric_;
   double price_;
   int volume_;

   friend class Exchange;
   friend class Strategy;
};

// dummy
template<typename T>
struct TypeTraits;

// specialization for T*
template <typename T>
struct TypeTraits<T*>
{
   using Type = T*;

   template <typename... Args>
   static Type create(Args&&... args)
   {
      return new T(std::forward<Args>(args)...);
   }

   static void destroy(Type t)
   {
      delete t;
      t = nullptr;
   }

   static constexpr bool is_unique_ptr(Type t) 
   {
      return false;
   }
};

// Specialization for unique_ptr<T>.
template <typename T>
struct TypeTraits<std::unique_ptr<T>>
{
   using Type = std::unique_ptr<T>;

   template <typename... Args>
   static Type create(Args&&... args)
   {
      return std::move(std::make_unique<T>(std::forward<Args>(args)...));
   }

   // Dummy as when it gets out of scope it will destroy by itself.
   static void destroy(Type& t)
   {
      //t.reset();
   }

   static constexpr bool is_unique_ptr(Type& t) 
   {
      return true;
   }
};

// If we pass objects onto the queue wrapped in unique_ptr.
using TradePtr = std::unique_ptr<Trade>;
// If we pass naked pointers onto the queue.
//using TradePtr = Trade*;

//using TASK_QUEUE = queue::Queue<TradePtr>;
using TASK_QUEUE = queue::CircularQueue<TradePtr>;

// To determine the correct type of TradePtr for
// construction and destruction.
using Traits = TypeTraits<TradePtr>;

// Distribution selection semantics...
//#if __cplusplus >= 202002L
template <typename T> struct distribution;
//#else
//template <typename T, typename X=void> struct distribution;
//#endif

// Pick this one if using int
template <typename T>
//#if __cplusplus >= 202002L
requires std::integral<T>
struct distribution<T>
//#else
//struct distribution<T, typename std::enable_if<std::is_integral<T>::value>::type>
//#endif
{
   using type = std::uniform_int_distribution<T>; 
};

// Pick this one if using double
template <typename T> 
//#if __cplusplus >= 202002L
requires std::floating_point<T>
struct distribution<T>
//#else
//struct distribution<T, typename std::enable_if<std::is_floating_point<T>::value>::type>
//#endif
{ 
   using type = std::uniform_real_distribution<T>; 
};

// Random number generator
template <typename T>
class Rand
{
public:
   Rand(T low, T high)
   : dist_{low,high} {}
   T operator()() { return dist_(re_); } 
   void seed(T s) { re_.seed(s); }
private:
   std::default_random_engine re_;
   // Get the right distribution based on T.
   using distribution_t = distribution<T>::type;
   distribution_t dist_;
};

#endif
