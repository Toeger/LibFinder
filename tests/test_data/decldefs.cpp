struct S {
	template <auto>
	static int i;
};

enum class E { a, b, c } extern e;

template <auto, class T>
void g(T &&);

namespace ns {
	template <auto, class T>
	void h(T &&);

	enum class E { a, b, c } extern e;

	template <class T>
	struct SS {
		void f() const &;

		SS();
		~SS();
	};
	extern SS<double> ss;
} // namespace ns

void f() {
	S::i<E::a> ++;
	ns::ss.f();
	e = E::b;
	ns::e = ns::E::b;
	g<f>(f);
	ns::h<f>(f);
	h<f>(ns::e);
	ns::SS<int> ss;
}
