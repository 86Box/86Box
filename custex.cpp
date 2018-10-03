#include <exception>
#include <iostream>
#include <string>

using namespace std;

enum {
    ABRT_GPF = 0,
    ABRT_PF,
    ABRT_SS,
    ABRT_TS,
    ABRT_NP,
    ABRT_DEBUG
};

enum class exception_type
{
    FAULT, TRAP, ABORT
};


struct Exception {

public:
   Exception(const string& msg) : msg_(msg) {}
   Exception(const string& msg, int type) : msg_(msg), type_(type) {}
  ~Exception() {}

   string getMessage() const {return(msg_);}
   int getType() const {return(type_);}
private:
   string msg_;
   int type_;
};

struct cpu_exception
{
    exception_type type;
    uint8_t fault_type;
    uint32_t error_code;
    bool error_code_valid;
    cpu_exception(exception_type _type, uint8_t _fault_type, uint32_t errcode, bool errcodevalid)
    : type(_type)
    , fault_type(_fault_type)
    , error_code(errcode)
    , error_code_valid(errcodevalid) {}

    cpu_exception(const cpu_exception& other) = default;
};

int main()
{
    try {
    	// throw(Exception("0000:2C0E", ABRT_PF));
	throw cpu_exception(exception_type::FAULT, ABRT_GPF, 0, true);
    } catch (Exception& ex) {
    	std::cout << "Type: " << ex.getType() << std::endl;
        return EXIT_FAILURE;
    } catch(const cpu_exception& e) {
	std::cout << "Error code: " << e.error_code << std::endl;
	return EXIT_FAILURE;
    }
}