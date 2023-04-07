#include "common.h"

///////////////////////////////////////
// SPI Functions
///////////////////////////////////////

static inline void trigger_spi(spi_inst_t *spi, uint baudrate) {
    //spi_init
    buff32_pointer = 0;
    reset_block(spi == spi0 ? RESETS_RESET_SPI0_BITS : RESETS_RESET_SPI1_BITS);
    unreset_block_wait(spi == spi0 ? RESETS_RESET_SPI0_BITS : RESETS_RESET_SPI1_BITS);

    spi_set_baudrate(spi, baudrate);    
    spi_set_format(spi, 8, SPI_CPOL_1, SPI_CPOL_1, SPI_MSB_FIRST);
    hw_set_bits(&spi_get_hw(spi)->dmacr, SPI_SSPDMACR_TXDMAE_BITS | SPI_SSPDMACR_RXDMAE_BITS);
    spi_set_slave(spi, true);
    
    // Set the first data into the buffer
    if(!is32bitsMode){
        spi_get_hw(spi)->dr = 0xD2;
    }else{
        spi_get_hw(spi)->dr = 0xD2;
        spi_get_hw(spi)->dr = 0xD2;
        spi_get_hw(spi)->dr = 0xD2;
        spi_get_hw(spi)->dr = 0xD2;
    } 

    hw_set_bits(&spi_get_hw(spi)->cr1, SPI_SSPCR1_SSE_BITS);
}

///////////////////////////////////////
// Mobile Adapter GB Functions
///////////////////////////////////////

unsigned long millis_latch = 0;
#define A_UNUSED __attribute__((unused))

////////////////////////
// Auxiliar functions
////////////////////////

void main_parse_addr(struct mobile_addr *dest, char *argv){
    unsigned char ip[MOBILE_INET_PTON_MAXLEN];
    int rc = mobile_inet_pton(MOBILE_INET_PTON_ANY, argv, ip);

    struct mobile_addr4 *dest4 = (struct mobile_addr4 *)dest;
    struct mobile_addr6 *dest6 = (struct mobile_addr6 *)dest;
    switch (rc) {
        case MOBILE_INET_PTON_IPV4:
            dest4->type = MOBILE_ADDRTYPE_IPV4;
            memcpy(dest4->host, ip, sizeof(dest4->host));
            break;
        case MOBILE_INET_PTON_IPV6:
            dest6->type = MOBILE_ADDRTYPE_IPV6;
            memcpy(dest6->host, ip, sizeof(dest6->host));
            break;
        default:
            break;
    }
}

static bool main_parse_hex(unsigned char *buf, char *str, unsigned size){
    unsigned char x = 0;
    for (unsigned i = 0; i < size * 2; i++) {
        char c = str[i];
        if (c >= '0' && c <= '9') c -= '0';
        else if (c >= 'A' && c <= 'F') c -= 'A' - 10;
        else if (c >= 'a' && c <= 'f') c -= 'a' - 10;
        else return false;

        x <<= 4;
        x |= c;

        if (i % 2 == 1) {
            buf[i / 2] = x;
            x = 0;
        }
    }
    return true;
}

void main_set_port(struct mobile_addr *dest, unsigned port){
    struct mobile_addr4 *dest4 = (struct mobile_addr4 *)dest;
    struct mobile_addr6 *dest6 = (struct mobile_addr6 *)dest;
    switch (dest->type) {
    case MOBILE_ADDRTYPE_IPV4:
        dest4->port = port;
        break;
    case MOBILE_ADDRTYPE_IPV6:
        dest6->port = port;
        break;
    default:
        break;
    }
}

/////////////////////////
// Libmobile Functions
/////////////////////////

static void impl_debug_log(void *user, const char *line){
    (void)user;
    fprintf(stderr, "%s\n", line);
}

static void impl_serial_disable(void *user) {
    #ifdef DEBUG_SIGNAL_PINS
    gpio_put(10, true);
    #endif
    (void)user;
    struct mobile_user *mobile = (struct mobile_user *)user;
    while(spiLock);

    spi_deinit(SPI_PORT);    
}

static void impl_serial_enable(void *user, bool mode_32bit) {
    (void)mode_32bit;
    struct mobile_user *mobile = (struct mobile_user *)user;
    //is32bitsMode = mode_32bit;
    //if(mode_32bit){
    //    trigger_spi(SPI_PORT,SPI_BAUDRATE_512);
    //}else{
        trigger_spi(SPI_PORT,SPI_BAUDRATE_256);
    //}
    #ifdef DEBUG_SIGNAL_PINS
    gpio_put(10, false);
    #endif
}

static bool impl_config_read(void *user, void *dest, const uintptr_t offset, const size_t size) {
    struct mobile_user *mobile = (struct mobile_user *)user;
    for(int i = 0; i < size; i++){
        ((char *)dest)[i] = (char)mobile->config_eeprom[OFFSET_MAGB + offset + i];
    }
    return true;
}

static bool impl_config_write(void *user, const void *src, const uintptr_t offset, const size_t size) {
    struct mobile_user *mobile = (struct mobile_user *)user;
    for(int i = 0; i < size; i++){
        mobile->config_eeprom[OFFSET_MAGB + offset + i] = ((uint8_t *)src)[i];
    }
    haveConfigToWrite = true;
    last_readable = time_us_64();
    return true;
}

static void impl_time_latch(A_UNUSED void *user, unsigned timer) {
    millis_latch = time_us_64();
}

static bool impl_time_check_ms(A_UNUSED void *user, unsigned timer, unsigned ms) {
    return (time_us_64() - millis_latch) > MS(ms);
}



static bool impl_sock_open(void *user, unsigned conn, enum mobile_socktype socktype, enum mobile_addrtype addrtype, unsigned bindport){
    // Returns: true if socket was created successfully, false on error
    struct mobile_user *mobile = (struct mobile_user *)user;
    printf("mobile_impl_sock_open\n");
   
    return true;
}

static void impl_sock_close(void *user, unsigned conn){
    struct mobile_user *mobile = (struct mobile_user *)user;
    printf("mobile_impl_sock_close\n");
}

static int impl_sock_connect(void *user, unsigned conn, const struct mobile_addr *addr){
    // Returns: 1 on success, 0 if connect is in progress, -1 on error
    struct mobile_user *mobile = (struct mobile_user *)user;
    printf("mobile_impl_sock_connect\n");

    return -1;
}

static bool impl_sock_listen(void *user, unsigned conn){
    // Returns: true if a connection was accepted,
    //          false if there's no incoming connections    
    struct mobile_user *mobile = (struct mobile_user *)user;
    printf("mobile_impl_sock_listen\n");

    return false;
}
static bool impl_sock_accept(void *user, unsigned conn){
    // Returns: true if a connection was accepted,
    //          false if there's no incoming connections
    struct mobile_user *mobile = (struct mobile_user *)user;
    printf("mobile_impl_sock_accept\n");

    return false;
}
 
static int impl_sock_send(void *user, unsigned conn, const void *data, const unsigned size, const struct mobile_addr *addr){    
    // Returns: non-negative amount of data sent on success, -1 on error
    struct mobile_user *mobile = (struct mobile_user *)user;
    printf("mobile_impl_sock_send\n");

    return -1;
}

static int impl_sock_recv(void *user, unsigned conn, void *data, unsigned size, struct mobile_addr *addr){
    // Returns: amount of data received on success,
    //          -1 on error,
    //          -2 on remote disconnect
    struct mobile_user *mobile = (struct mobile_user *)user;
    struct mobile_addr4 *addr4 = (struct mobile_addr4 *)addr;
    struct mobile_addr6 *addr6 = (struct mobile_addr6 *)addr;
    printf("mobile_impl_sock_recv\n");
  
    return -2;
}

static void impl_update_number(void *user, enum mobile_number type, const char *number){
    struct mobile_user *mobile = (struct mobile_user *)user;

    char *dest = NULL;
    switch (type) {
        case MOBILE_NUMBER_USER: dest = mobile->number_user; break;
        case MOBILE_NUMBER_PEER: dest = mobile->number_peer; break;
        default: assert(false); return;
    }

    if (number) {
        strncpy(dest, number, MOBILE_MAX_NUMBER_SIZE);
        dest[MOBILE_MAX_NUMBER_SIZE] = '\0';
    } else {
        dest[0] = '\0';
    }
}

