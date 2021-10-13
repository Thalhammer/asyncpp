#pragma once
#include <asyncpp/detail/std_import.h>
#include <variant>

namespace asyncpp {
	template<class T>
	class task;

	namespace detail {
		template<class TVal, class TPromise>
		class task_promise_base {
		public:
			task_promise_base() noexcept {}
			~task_promise_base() {}
			task_promise_base(const task_promise_base&) = delete;
			task_promise_base(task_promise_base&&) = delete;
			task_promise_base& operator=(const task_promise_base&) = delete;
			task_promise_base& operator=(task_promise_base&&) = delete;

			coroutine_handle<TPromise> get_return_object() noexcept { return coroutine_handle<TPromise>::from_promise(*static_cast<TPromise*>(this)); }

			suspend_always initial_suspend() { return {}; }
			auto final_suspend() noexcept {
				struct awaiter {
					bool await_ready() noexcept { return false; }
					auto await_suspend(coroutine_handle<TPromise> h) noexcept {
						assert(h);
						assert(h.promise().m_continuation);
						return h.promise().m_continuation;
					}
					void await_resume() noexcept {}
				};
				return awaiter{};
			}

			void unhandled_exception() noexcept { m_value.template emplace<std::exception_ptr>(std::current_exception()); }

			TVal rethrow_if_exception() {
				if (std::holds_alternative<std::exception_ptr>(m_value)) std::rethrow_exception(std::get<std::exception_ptr>(m_value));
				return std::get<TVal>(std::move(this->m_value));
			}

			coroutine_handle<> m_continuation;
			std::variant<std::monostate, TVal, std::exception_ptr> m_value;
		};

		template<class T>
		class task_promise : public task_promise_base<T, task_promise<T>> {
		public:
			template<class U>
			void return_value(U&& value) requires(std::is_convertible_v<U, T>) {
				this->m_value.template emplace<T>(value);
			}
			T get() { return this->rethrow_if_exception(); }
		};

		struct returned {};
		template<>
		class task_promise<void> : public task_promise_base<returned, task_promise<void>> {
		public:
			void return_void() { m_value.template emplace<returned>(); }
			void get() { this->rethrow_if_exception(); }
		};
	} // namespace detail

	/**
	 * \brief Generic task type
	 * \tparam T Return type of the task
	 * \note You should always mark functions returning task<> as [[nodiscard]] to avoid not co_awaiting them.
	 */
	template<class T = void>
	class [[nodiscard]] task {
	public:
		/// \brief Promise type
		using promise_type = detail::task_promise<T>;
		/// \brief Handle type
		using handle_t = coroutine_handle<promise_type>;

		/// \brief Construct from handle
		task(handle_t h) noexcept : m_coro(h) {
			assert(m_coro);
			assert(!m_coro.done());
		}

		/// \brief Construct from nullptr. The resulting task is invalid.
		task(std::nullptr_t) noexcept : m_coro{} {}

		/// \brief Move constructor
		task(task && other) noexcept : m_coro{std::exchange(other.m_coro, {})} {}
		/// \brief Move assignment
		task& operator=(task&& other) noexcept {
			m_coro = std::exchange(other.m_coro, m_coro);
			return *this;
		}
		task(const task&) = delete;
		task& operator=(const task&) = delete;

		/// \brief Destructor
		~task() {
			if (m_coro) m_coro.destroy();
			m_coro = nullptr;
		}

		/// \brief Check if the task holds a valid coroutine.
		explicit operator bool() const noexcept { return m_coro != nullptr; }
		/// \brief Check if the task does not hold a valid coroutine.
		bool operator!() const noexcept { return m_coro == nullptr; }

		/// \brief Operator co_await
		auto operator co_await() noexcept {
			struct awaiter {
				explicit awaiter(handle_t coro) : m_coro(coro) {}
				bool await_ready() noexcept { return false; }
				auto await_suspend(coroutine_handle<void> h) noexcept {
					assert(m_coro);
					assert(h);
					m_coro.promise().m_continuation = h;
					return m_coro;
				}
				T await_resume() {
					assert(m_coro);
					return m_coro.promise().get();
				}

			private:
				handle_t m_coro;
			};
			assert(m_coro);
			return awaiter{m_coro};
		}

		/**
		 * \brief Release the coroutine handle from this task. You are now responsible for managing its livetime.
		 * \return The contained coroutine handle.
		 */
		handle_t release() noexcept {
			assert(m_coro);
			return std::exchange(m_coro, {});
		}

	private:
		handle_t m_coro;
	};
} // namespace asyncpp
