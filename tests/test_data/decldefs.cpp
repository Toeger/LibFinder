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
} // namespace ns

void f() {
	S::i<E::a> ++;
	e = E::b;
	ns::e = ns::E::b;
	g<f>(f);
	ns::h<f>(f);
	h<f>(ns::e);
}
