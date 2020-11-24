#ifndef ERRCODE_H
#define ERRCODE_H


enum Status {
    SUCCESS = 0,
    UNKNOWN_OP,
    INVALID_OP,
    MSGR_EAGAIN = 16,
    MSGR_CONNECT_EXCEPTION,
    OSTORE_UNSUPPORTED_OPERATION ,
    OSTORE_OBJECT_EXIST,
    OSTORE_OBJECT_NOT_EXIST,
    OSTORE_NO_SPACE,
    OSTORE_NO_NODE,
    OSTORE_WRITE_OUT_MAX_SIZE,
    OSTORE_READ_EOF,
    OSTORE_READ_HOLE,
    OSTORE_IO_ERROR,
    OSTORE_INTERNAL_UNKNOWN_ERRORR
};

static inline const char *errcode_str(int s) {
    switch (s) {
        case SUCCESS:
            return "SUCCESS";    
        case UNKNOWN_OP:
            return "UNKNOWN_OP";
        case INVALID_OP:
            return "INVALID_OP";
        case MSGR_EAGAIN:
            return "MSGR_EAGAIN";
        case MSGR_CONNECT_EXCEPTION:
            return "MSGR_CONNECT_EXCEPTION";
        case OSTORE_UNSUPPORTED_OPERATION :
            return "OSTORE_UNSUPPORTED_OPERATION";
        case OSTORE_OBJECT_EXIST:
            return "OSTORE_OBJECT_EXIST";
        case OSTORE_OBJECT_NOT_EXIST:
            return "OSTORE_OBJECT_NOT_EXIST";
        case OSTORE_NO_SPACE:
            return "OSTORE_NO_SPACE";
        case OSTORE_NO_NODE:
            return "OSTORE_NO_NODE";
        case OSTORE_WRITE_OUT_MAX_SIZE:
            return "OSTORE_WRITE_OUT_MAX_SIZE";
        case OSTORE_READ_EOF:
            return "OSTORE_READ_EOF";
        case OSTORE_IO_ERROR:
            return "OSTORE_IO_ERROR";
        case OSTORE_INTERNAL_UNKNOWN_ERRORR:
            return "OSTORE_INTERNAL_UNKNOWN_ERRORR";
        default:
            return "????";
    }
}

#define OSTORE_SUBMIT_OK SUCCESS
#define OSTORE_EXECUTE_OK SUCCESS




#endif