/* Exercise the production registration, removal and input dispatch helpers. */
#include "../../src/sound/midi.c"

static unsigned checks, failures;
#define CHECK(expr) do { checks++; if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); failures++; \
} } while (0)

typedef struct {
    unsigned messages, sysex;
} receiver_t;

void fatal(const char *fmt, ...)
{ (void)fmt; abort(); }
const device_t device_none = { .name = "None", .internal_name = "none" };
int device_available(const device_t *dev) { (void)dev; abort(); }
void *device_add(const device_t *dev) { (void)dev; abort(); }
void device_close(const device_t *dev) { (void)dev; abort(); }
const char *device_get_internal_name(const device_t *dev)
{ (void)dev; abort(); }
void closeal(void) { abort(); }
void inital(void) { abort(); }
uint32_t plat_get_ticks(void) { return 0; }
void plat_delay_ms(uint32_t delay) { (void)delay; abort(); }
static int room(void *priv) { (void)priv; return 1024; }
static int other_room(void *priv) { (void)priv; return 1024; }
static void receive(void *priv, uint8_t *msg, uint32_t len)
{
    receiver_t *r = priv;
    CHECK(len == 3 && msg[0] == 0x90 && msg[1] == 60 && msg[2] == 100);
    r->messages++;
}
static void other_receive(void *priv, uint8_t *msg, uint32_t len)
{ receive(priv, msg, len); }
static int receive_sysex(void *priv, uint8_t *msg, uint32_t len, int abort)
{
    receiver_t *r = priv;
    CHECK(!abort && len == 2 && msg[0] == 0xf0 && msg[1] == 0xf7);
    r->sysex++;
    return 0;
}
static int other_sysex(void *priv, uint8_t *msg, uint32_t len, int abort)
{ return receive_sysex(priv, msg, len, abort); }
static void attach(receiver_t *r)
{ midi_in_handler(1, receive, receive_sysex, room, r); }
static void detach(receiver_t *r)
{ midi_in_handler(0, receive, receive_sysex, room, r); }
static void dispatch(void)
{
    uint8_t message[] = {0x90, 60, 100}, sysex[] = {0xf0, 0xf7};
    midi_in_msg(message, sizeof(message));
    midi_in_sysex(sysex, sizeof(sysex));
}
static void check_chain(receiver_t **expected, unsigned count)
{
    midi_in_handler_t *node = mih_first, *previous = NULL;
    for (unsigned i = 0; i < count; i++) {
        CHECK(node != NULL);
        if (!node) return;
        CHECK((void *)node->priv == expected[i] && node->prev == previous);
        previous = node;
        node = node->next;
    }
    CHECK(node == NULL && mih_last == previous);
}

int main(void)
{
    receiver_t a = {0}, b = {0}, c = {0}, absent = {0};
    attach(&a); attach(&b); attach(&c);
    check_chain((receiver_t *[]){&a, &b, &c}, 3);
    dispatch();
    CHECK(a.messages == 1 && b.messages == 1 && c.messages == 1);
    CHECK(a.sysex == 1 && b.sysex == 1 && c.sysex == 1);

    /* Missing or mismatched identities must return without deleting nodes. */
    detach(&absent);
    midi_in_handler(0, other_receive, receive_sysex, room, &b);
    midi_in_handler(0, receive, other_sysex, room, &b);
    midi_in_handler(0, receive, receive_sysex, other_room, &b);
    check_chain((receiver_t *[]){&a, &b, &c}, 3);

    detach(&b); /* Middle, while preserving both neighbors. */
    check_chain((receiver_t *[]){&a, &c}, 2);
    dispatch();
    CHECK(a.messages == 2 && b.messages == 1 && c.messages == 2);
    CHECK(a.sysex == 2 && b.sysex == 1 && c.sysex == 2);
    detach(&c); /* Tail, then append again to the surviving list. */
    check_chain((receiver_t *[]){&a}, 1);
    attach(&b);
    check_chain((receiver_t *[]){&a, &b}, 2);
    detach(&a); /* Head, while preserving its successor. */
    check_chain((receiver_t *[]){&b}, 1);
    dispatch();
    CHECK(a.messages == 2 && b.messages == 2 && c.messages == 2);
    CHECK(a.sysex == 2 && b.sysex == 2 && c.sysex == 2);
    detach(&b); /* The only remaining node. */
    check_chain(NULL, 0);
    detach(&b);
    dispatch();
    CHECK(b.messages == 2 && b.sysex == 2);

    /* Preserve removal of all registrations with exactly this identity. */
    attach(&a); attach(&a); attach(&b);
    detach(&a);
    check_chain((receiver_t *[]){&b}, 1);
    attach(&c);
    midi_in_handlers_clear();
    check_chain(NULL, 0);
    dispatch();
    CHECK(b.messages == 2 && c.messages == 2);
    printf("MIDI handlers: %u checks, %u failures\n", checks, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
