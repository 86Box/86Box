#include <QAbstractNativeEventFilter>
#include <QByteArray>

class CocoaEventFilter : public QAbstractNativeEventFilter
{
public:
    CocoaEventFilter() {};
    ~CocoaEventFilter();
    virtual bool nativeEventFilter(const QByteArray &eventType, void *message, long *result) override;
};
