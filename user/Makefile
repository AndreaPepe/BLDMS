# put here the arguments that will be passed to the user program 
DEVICE_FILEPATH = "../mount/the_file"
PUT_DATA_NR = 134
GET_DATA_NR = 156
INVALIDATE_DATA_NR = 174

all:
	gcc user.c -o user
	gcc user_concurrency.c -lpthread -o user_concurrency
	gcc test.c -lpthread -o test

clean:
	rm user
	rm user_concurrency
	rm test

run:
	./user $(DEVICE_FILEPATH) $(PUT_DATA_NR) $(GET_DATA_NR) $(INVALIDATE_DATA_NR)

run_concurrency:
	./user_concurrency $(DEVICE_FILEPATH) $(PUT_DATA_NR) $(GET_DATA_NR) $(INVALIDATE_DATA_NR)

run_test:
	./test $(DEVICE_FILEPATH) $(PUT_DATA_NR) $(GET_DATA_NR) $(INVALIDATE_DATA_NR)