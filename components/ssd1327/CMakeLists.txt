idf_component_register(SRCS "ssd1327.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "."
                       REQUIRES hw_map i2cbus)
