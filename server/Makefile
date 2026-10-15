.PHONY: clean all


CC=$(CROSS_COMPILE)gcc
APP_NAME=aesdsocket
BASE_DIR=$(realpath .)
BUILD_DIR=$(BASE_DIR)
SRC_DIRS=$(BASE_DIR)
SRC_FILES=$(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c))
SRC_NAMES=$(foreach file,$(SRC_FILES),$(notdir $(file)))
OBJ_FILES=$(foreach name,$(SRC_NAMES),$(patsubst %.c,${BUILD_DIR}/%.o,$(name)))
LDFLAGS=
CFLAGS= -g
VPATH = $(SRC_DIRS)

all: ${BUILD_DIR} $(OBJ_FILES)
	$(CC) -o $(BUILD_DIR)/$(APP_NAME) $(OBJ_FILES)
	@echo "Done building aesdsocket app"

$(BUILD_DIR):
	@mkdir -p ${BUILD_DIR}

$(BUILD_DIR)/%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<

clean:
	@echo "Cleaning..."
	@rm -rf $(BUILD_DIR)/*.o $(BUILD_DIR)/$(APP_NAME)
