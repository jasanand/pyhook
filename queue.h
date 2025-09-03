#ifndef QUEUE_INCLUDE
#define QUEUE_INCLUDE

#include <stdint.h>
#include <atomic>
#include <iostream>

namespace queue
{

namespace queue_detail
{

static constexpr int  eCacheLineSize = 64;

// users can put whatever type they choose into the queue.  If the user is
// supplying a pointer or unique_ptr, then he/she must have already put the 
// object on the heap and there is no need for us to do so.  However, if we're 
// not dealing with pointer types, then we want to put it on the heap to ensure 
// our nodes fit on their own cache line
template<typename T>
struct TypeTraits
{
   using Type = T;
   using PtrType = Type*;

   static PtrType create(const Type& t)
   {
      return new Type(t);
   }

   static Type convert(PtrType t)
   {
      return *t;
   }

   static PtrType get(PtrType t)
   {
      return t;
   }

   static void destroy(const PtrType t)
   {
      delete t;
   }
};

// pick this one if we're being instantiated with a pointer type.  User must
// have already put object on the heap.
//------------------------------------------------------------------------------
template<typename T>
struct TypeTraits<T*>
{
   using Type = T*;
   using PtrType = Type;

   static PtrType create(const Type& t)
   {
      return t;
   }

   static Type convert(PtrType t)
   {
      return t;
   }

   static PtrType get(PtrType t)
   {
      return t;
   }

   static void destroy(const PtrType)
   {
   }
};

// pick this one if we're being instantiated with a unique_pointer type
// the trick is to just move ownership among unique_ptr variables
template<typename T>
struct TypeTraits<std::unique_ptr<T>>
{
   using Type = std::unique_ptr<T>;
   using PtrType = Type;

   static PtrType create(const Type& t)
   {
      // Need to remove constness, get the 
      // unique_ptr and move.
      return std::move(const_cast<Type&>(t));
   }

   static Type convert(PtrType& t)
   {
      return std::move(t);
   }

   static PtrType get(PtrType& t)
   {
      return std::move(t);
   }

   static void destroy(const PtrType& t)
   {
   }
};

} // end of namespace queue_detail

template<typename T>
class Queue
{
public:

   Queue()
   {
      consumer_lock_.store(UNLOCKED);
      producer_lock_.store(UNLOCKED);

      // dummy starting node, null value, null next
      Node* divider = new Node(nullptr);
      first_.store(divider);
      last_.store(divider);
   }

   ~Queue()
   {
      while(nullptr != first_.load())
      {
         Node* tmp = first_.load();
         first_.store(tmp->next);
         Traits::destroy(tmp->value);
         delete tmp;
         tmp = nullptr;
      }
   }

   // Push an element onto the queue
   void enqueue(const T& t)
   {
      // Create the new node before trying to grab the lock
      Node* new_node = new Node(Traits::create(t));

      bool expected = UNLOCKED;
      // we check if the current value is false (UNLOCKED), if not then we wait until 
      // it is set false (UNLOCKED) later
      // and then this thread takes hold by setting it to true (LOCKED)
      while(!producer_lock_.compare_exchange_weak(expected, LOCKED, std::memory_order_acquire)) 
      {
         expected = UNLOCKED;
         // instead of busy spinning we can wait until notified
         // as under the hood the wait call may spin for a bit 
         // and then use futex
         producer_lock_.wait(true, std::memory_order_relaxed);
      }

      // Publish the new node
      last_.load()->next.store(new_node);
      last_.store(new_node);

      producer_lock_.store(UNLOCKED, std::memory_order_release);
      producer_lock_.notify_all();
   }

   // Attempt to remove an element from the queue.  Will return true if the
   // passed in reference was updated.
   bool dequeue(T& result)
   {
      // we check if the current value is false (UNLOCKED), if not then we wait until 
      // it is set false (UNLOCKED) later
      // and then this thread takes hold by setting it to true (LOCKED)
      bool expected = UNLOCKED;
      while(!consumer_lock_.compare_exchange_weak(expected, LOCKED, std::memory_order_acquire)) 
      {
         expected = UNLOCKED;
         // instead of busy spinning we can wait until notified
         // as under the hood the wait call may spin for a bit 
         // and then use futex
         consumer_lock_.wait(true, std::memory_order_relaxed);
      }

      Node* the_first = first_.load();
      Node* the_next = the_first->next.load();

      if(the_next != nullptr)
      {
         // Fetch the value out and move the first pointer
         PtrType the_value = Traits::get(the_next->value);
         the_next->value = nullptr;
         first_.store(the_next);

         // Release the lock so others can gain access
         consumer_lock_.store(UNLOCKED, std::memory_order_release);
         consumer_lock_.notify_all();

         // Assign the value to result and clean up
         result = Traits::convert(the_value);
         Traits::destroy(the_value);
         delete the_first;
         the_first = nullptr;

         return true;
      }

      consumer_lock_.store(UNLOCKED, std::memory_order_release);
      consumer_lock_.notify_all();
      return false;
   }

private:

   // Select a traits type from above.
   using Traits = queue_detail::TypeTraits<T>;

   // Get the type the user instantiated us with and the type of a pointer
   // They'll be the same if we were instantiated with a pointer type
   using UserType = Traits::Type;
   using PtrType = Traits::PtrType;

   struct Node;

   // need the next of Node to be atomic
   using AtomicNodePtr = std::atomic<Node*>;

   struct Node
   {
      Node(PtrType val)
      : value(Traits::get(val)), next(nullptr) {}
      PtrType value;
      AtomicNodePtr next;
   };

private:

   char pad0[queue_detail::eCacheLineSize];

   // For one consumer at a time
   alignas(queue_detail::eCacheLineSize) AtomicNodePtr first_;
   alignas(queue_detail::eCacheLineSize) std::atomic<bool> consumer_lock_;

   // For one producer at a time
   alignas(queue_detail::eCacheLineSize) AtomicNodePtr last_;
   alignas(queue_detail::eCacheLineSize) std::atomic<bool> producer_lock_;

   static constexpr bool LOCKED = true;
   static constexpr bool UNLOCKED = false;
};


} // end of namespace

#endif
