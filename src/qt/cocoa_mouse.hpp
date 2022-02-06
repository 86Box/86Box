#include <QAbstractNativeEventFilter>
#include <QByteArray>

#if QT_VERSION_MAJOR >= 6
#define result_t qintptr
#else
#define result_t long
#endif

class CocoaEventFilter : public QAbstractNativeEventFilter
{
public:
    CocoaEventFilter() {};
    ~CocoaEventFilter();
    virtual bool nativeEventFilter(const QByteArray &eventType, void *message, result_t *result) override;
};
