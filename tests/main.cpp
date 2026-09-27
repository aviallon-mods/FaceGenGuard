#include "harness.h"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <new>
#if defined(_WIN32)
#	include <malloc.h>  // _aligned_malloc / _aligned_free: MSVC's CRT has NO std::aligned_alloc
#endif

// ---------------------------------------------------------------------------
// The heap probe: replaceable global operator new/delete overloads that count
// every allocation and delegate to malloc/free. This is how "the handler path
// makes no heap allocation" becomes a CHECKED claim instead of a code-reading
// claim. Every replaceable form is covered (array, sized, aligned), so an
// allocation cannot slip through an un-overridden overload.
// ---------------------------------------------------------------------------
namespace
{
	std::atomic<long long> g_allocations{ 0 };
	std::atomic<long long> g_frees{ 0 };

	void* ProbeAllocate(std::size_t a_size, std::size_t a_alignment)
	{
		g_allocations.fetch_add(1, std::memory_order_relaxed);
		if (a_size == 0) {
			a_size = 1;
		}
		void* result = nullptr;
		if (a_alignment > alignof(std::max_align_t)) {
			// std::aligned_alloc requires size to be a multiple of alignment.
			const auto rounded = (a_size + a_alignment - 1) / a_alignment * a_alignment;
#if defined(_WIN32)
			result = _aligned_malloc(rounded, a_alignment);  // MSVC: no std::aligned_alloc at all
#else
			result = std::aligned_alloc(a_alignment, rounded);
#endif
		} else {
			result = std::malloc(a_size);
		}
		if (result == nullptr) {
			throw std::bad_alloc{};
		}
		return result;
	}

	void ProbeFree(void* a_ptr, std::size_t a_alignment) noexcept
	{
		if (a_ptr != nullptr) {
			g_frees.fetch_add(1, std::memory_order_relaxed);
#if defined(_WIN32)
			if (a_alignment > alignof(std::max_align_t)) {
				_aligned_free(a_ptr);  // must pair with _aligned_malloc
				return;
			}
#endif
			std::free(a_ptr);
		}
	}
}

void* operator new(std::size_t a_size) { return ProbeAllocate(a_size, 0); }
void* operator new[](std::size_t a_size) { return ProbeAllocate(a_size, 0); }
void* operator new(std::size_t a_size, std::align_val_t a_alignment) { return ProbeAllocate(a_size, static_cast<std::size_t>(a_alignment)); }
void* operator new[](std::size_t a_size, std::align_val_t a_alignment) { return ProbeAllocate(a_size, static_cast<std::size_t>(a_alignment)); }

void operator delete(void* a_ptr) noexcept { ProbeFree(a_ptr, 0); }
void operator delete[](void* a_ptr) noexcept { ProbeFree(a_ptr, 0); }
void operator delete(void* a_ptr, std::size_t) noexcept { ProbeFree(a_ptr, 0); }
void operator delete[](void* a_ptr, std::size_t) noexcept { ProbeFree(a_ptr, 0); }
void operator delete(void* a_ptr, std::align_val_t a_alignment) noexcept { ProbeFree(a_ptr, static_cast<std::size_t>(a_alignment)); }
void operator delete[](void* a_ptr, std::align_val_t a_alignment) noexcept { ProbeFree(a_ptr, static_cast<std::size_t>(a_alignment)); }
void operator delete(void* a_ptr, std::size_t, std::align_val_t a_alignment) noexcept { ProbeFree(a_ptr, static_cast<std::size_t>(a_alignment)); }
void operator delete[](void* a_ptr, std::size_t, std::align_val_t a_alignment) noexcept { ProbeFree(a_ptr, static_cast<std::size_t>(a_alignment)); }

namespace hstest
{
	std::vector<TestCase>& Registry()
	{
		static std::vector<TestCase> registry;
		return registry;
	}

	int& CheckCount()
	{
		static int count = 0;
		return count;
	}

	int& FailureCount()
	{
		static int count = 0;
		return count;
	}

	void Fail(const char* a_file, int a_line, const std::string& a_message)
	{
		++FailureCount();
		std::fprintf(stderr, "  FAIL %s:%d: %s\n", a_file, a_line, a_message.c_str());
	}

	void Note(const std::string& a_message)
	{
		std::fprintf(stdout, "  note: %s\n", a_message.c_str());
	}

	long long HeapAllocations()
	{
		return g_allocations.load(std::memory_order_relaxed);
	}

	long long HeapFrees()
	{
		return g_frees.load(std::memory_order_relaxed);
	}

	int RunAll(const char* a_filter)
	{
		int ran = 0;
		int failedTests = 0;

		for (const auto& test : Registry()) {
			if (a_filter != nullptr && std::strstr(test.name, a_filter) == nullptr) {
				continue;
			}

			++ran;
			const int before = FailureCount();

			std::fprintf(stdout, "[ RUN  ] %s\n", test.name);
			std::fflush(stdout);
			test.fn();

			const bool ok = FailureCount() == before;
			std::fprintf(stdout, "[ %s ] %s\n", ok ? " OK " : "FAIL", test.name);
			if (!ok) {
				++failedTests;
			}
		}

		std::fprintf(stdout, "\n%d test(s) run, %d check(s), %d failure(s)\n",
			ran, CheckCount(), FailureCount());

		if (ran == 0) {
			// A filter that matches nothing must not look like success.
			std::fprintf(stderr, "no test matched the filter\n");
			return 2;
		}
		return FailureCount() == 0 ? 0 : 1;
	}
}

int main(int argc, char** argv)
{
	return hstest::RunAll(argc > 1 ? argv[1] : nullptr);
}
