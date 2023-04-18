# put here the arguments that will be passed to the user program 
DEVICE_FILEPATH = "../mount/the_file"
PUT_DATA_NR = 134
GET_DATA_NR = 135
INVALIDATE_DATA_NR = 136

all:
	gcc user.c -o user -lpthread

clean:
	rm user

run:
	./user $(DEVICE_FILEPATH) $(PUT_DATA_NR) $(GET_DATA_NR) $(INVALIDATE_DATA_NR)