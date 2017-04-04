#  C++ coding test

You have inherited a piece of code which tries to implement multithreaded producer/consumer.

It looks like the implementation is broken and the author is not available, so you are tasked with fixing the code and make it as fast as possible.

You should replace implementations of two functions – produce() and consume()
 

produce() will be running in parallel in multiple threads and it takes a single parameter – a function which returns a new record on every invocation. The record is a pointer to an array of bytes with the first byte is the length of the rest of the array. For example, if the first byte is 255 then the total length of the array is 256 bytes. The function returns nullptr when there is no more records, in which case the produce() function should terminate.

 

Consume() function should receive all records from all producers and should return a pair of  numbers – the first number is a XOR of all data bytes from all records (not including the first byte with length of the record). The second number is a total number of records processed by the consumer.

 

Expected results:

1.The implementation should work correctly, without possibility of a deadlock or race conditions.

2.The performance is important, it’s expected to finish processing all records as fast as possible.

3.Identification of problems with current implementation is a plus.

4.You will be expected to explain your code, describe alternative approaches and provide rationale for your choices.
 

The resulting program takes 3 command line arguments  random generator seed, number of threads and maximum number of records produced by each thread.
