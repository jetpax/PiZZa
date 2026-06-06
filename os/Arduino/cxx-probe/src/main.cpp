#include <zephyr/kernel.h>
#include <array>
#include <numeric>

class Stream {
public:
	virtual void print(const char *s) = 0;
	virtual ~Stream() = default;
};

class UartStream : public Stream {
	const char *tag_;
public:
	explicit UartStream(const char *tag) : tag_(tag) {}
	void print(const char *s) override {
		printk("[%s] %s\n", tag_, s);
	}
};

static UartStream g_serial{"Serial"};

static int counter()
{
	static int n = 0;
	return ++n;
}

int main(void)
{
	g_serial.print("global C++ object ctor ran");

	UartStream local{"local"};
	Stream &ref = local;
	ref.print("virtual dispatch via base ref");

	std::array<int, 5> arr{2, 3, 5, 7, 11};
	int sum = std::accumulate(arr.begin(), arr.end(), 0);
	printk("std::array<int,5> sum = %d (expect 28)\n", sum);

	for (int i = 0; i < 3; ++i) {
		printk("counter() = %d\n", counter());
	}

	printk("pizza-cxx-probe OK\n");
	return 0;
}
