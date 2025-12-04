/* file_transfer.x : RPC definition for simple file transfer */

const MAXFILESIZE = 1048576;   /* 1MB */

typedef string filename_t<>;   /* tên file truyền lên server */
typedef opaque filedata_t<>;   /* dữ liệu file dưới dạng byte array */

struct file_result {
    int status;       /* 0 = OK, !=0 = errno */
    filedata_t data;  /* dữ liệu file nếu status == 0 */
};

program FILE_TRANSFER_PROG {
    version FILE_TRANSFER_VERS {
        file_result GET_FILE(filename_t) = 1;
    } = 1;
} = 0x31234567;
