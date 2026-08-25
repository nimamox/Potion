use katex::{KatexContext, OutputFormat, Settings, render_to_string};
use std::collections::HashMap;
use std::ffi::{CStr, CString, c_char};
use std::hash::{Hash, Hasher};
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::ptr;
use std::sync::{Mutex, OnceLock};

const MAX_ENTRIES: usize = 192;
const MAX_BYTES: usize = 384 * 1024;

#[derive(Clone, Eq)]
struct CacheKey {
    expression: String,
    display_mode: bool,
}

impl PartialEq for CacheKey {
    fn eq(&self, other: &Self) -> bool {
        self.display_mode == other.display_mode && self.expression == other.expression
    }
}

impl Hash for CacheKey {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.display_mode.hash(state);
        self.expression.hash(state);
    }
}

struct CacheEntry {
    html: String,
    last_used: u64,
    bytes: usize,
}

#[cfg(test)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
struct CacheStats {
    hits: u64,
    misses: u64,
    evictions: u64,
    entries: usize,
    bytes: usize,
}

struct MathCache {
    entries: HashMap<CacheKey, CacheEntry>,
    clock: u64,
    bytes: usize,
    hits: u64,
    misses: u64,
    evictions: u64,
    max_entries: usize,
    max_bytes: usize,
}

impl MathCache {
    fn new(max_entries: usize, max_bytes: usize) -> Self {
        Self {
            entries: HashMap::new(),
            clock: 0,
            bytes: 0,
            hits: 0,
            misses: 0,
            evictions: 0,
            max_entries,
            max_bytes,
        }
    }

    fn get(&mut self, key: &CacheKey) -> Option<String> {
        self.clock = self.clock.wrapping_add(1);
        if let Some(entry) = self.entries.get_mut(key) {
            self.hits += 1;
            entry.last_used = self.clock;
            return Some(entry.html.clone());
        }
        self.misses += 1;
        None
    }

    fn insert(&mut self, key: CacheKey, html: String) {
        let bytes = key.expression.len() + html.len() + 1;
        if self.max_entries == 0 || bytes > self.max_bytes {
            return;
        }
        self.clock = self.clock.wrapping_add(1);
        if let Some(old) = self.entries.remove(&key) {
            self.bytes -= old.bytes;
        }
        while self.entries.len() >= self.max_entries || self.bytes + bytes > self.max_bytes {
            let Some(oldest) = self
                .entries
                .iter()
                .min_by_key(|(_, entry)| entry.last_used)
                .map(|(key, _)| key.clone())
            else {
                break;
            };
            if let Some(removed) = self.entries.remove(&oldest) {
                self.bytes -= removed.bytes;
                self.evictions += 1;
            }
        }
        self.bytes += bytes;
        self.entries.insert(
            key,
            CacheEntry {
                html,
                last_used: self.clock,
                bytes,
            },
        );
    }

    #[cfg(test)]
    fn stats(&self) -> CacheStats {
        CacheStats {
            hits: self.hits,
            misses: self.misses,
            evictions: self.evictions,
            entries: self.entries.len(),
            bytes: self.bytes,
        }
    }
}

static CONTEXT: OnceLock<KatexContext> = OnceLock::new();
static CACHE: OnceLock<Mutex<MathCache>> = OnceLock::new();

fn cache() -> &'static Mutex<MathCache> {
    CACHE.get_or_init(|| Mutex::new(MathCache::new(MAX_ENTRIES, MAX_BYTES)))
}

fn render_uncached(expression: &str, display_mode: bool) -> Result<String, String> {
    let settings = Settings::builder()
        .display_mode(display_mode)
        .output(OutputFormat::Html)
        .throw_on_error(true)
        .build();
    render_to_string(
        CONTEXT.get_or_init(KatexContext::default),
        expression,
        &settings,
    )
    .map_err(|error| error.to_string())
}

fn render_cached(expression: &str, display_mode: bool) -> Result<String, String> {
    let key = CacheKey {
        expression: expression.to_owned(),
        display_mode,
    };
    if let Some(html) = cache()
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
        .get(&key)
    {
        return Ok(html);
    }
    let html = render_uncached(expression, display_mode)?;
    cache()
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
        .insert(key, html.clone());
    Ok(html)
}

fn ffi_boundary<F>(operation: F) -> *mut c_char
where
    F: FnOnce() -> Result<String, String>,
{
    match catch_unwind(AssertUnwindSafe(operation)) {
        Ok(Ok(html)) => CString::new(html).map_or(ptr::null_mut(), CString::into_raw),
        Ok(Err(error)) => {
            eprintln!("Potion math render failed: {error}");
            ptr::null_mut()
        }
        Err(_) => {
            eprintln!("Potion math renderer panicked");
            ptr::null_mut()
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn potion_math_render(latex: *const c_char, display_mode: u8) -> *mut c_char {
    ffi_boundary(|| {
        if latex.is_null() {
            return Err("null LaTeX pointer".to_owned());
        }
        // SAFETY: the caller promises a valid NUL-terminated string for this call.
        let expression = unsafe { CStr::from_ptr(latex) }
            .to_str()
            .map_err(|_| "LaTeX is not valid UTF-8".to_owned())?;
        render_cached(expression, display_mode != 0)
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn potion_math_free(html: *mut c_char) {
    if !html.is_null() {
        // SAFETY: this pointer was returned by CString::into_raw above.
        drop(unsafe { CString::from_raw(html) });
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::thread;

    const EXPRESSIONS: &[&str] = &[
        "x",
        "x^2",
        "x_i^2",
        r"\alpha + \beta",
        r"\frac{1}{2}",
        r"\frac{a+b}{c+d}",
        r"\sqrt{x^2+y^2}",
        r"\sum_{k=1}^{N} x_k",
        r"\int_0^\infty e^{-x}\,dx",
        r"\left(\frac{x}{y}\right)^2",
        r"\operatorname{argmax}_x f(x)",
        r"\begin{bmatrix}a & b \\ c & d\end{bmatrix}",
        r"\boxed{x[n]=\sum_m x[m]\delta[n-m]}",
        r"n_{r,\min}=n_{s,\min}+n_{h,\min}",
    ];

    #[test]
    fn representative_html_only_rendering() {
        for expression in EXPRESSIONS {
            let html =
                render_uncached(expression, false).unwrap_or_else(|e| panic!("{expression}: {e}"));
            assert!(html.contains("class=\"katex\""));
            assert!(!html.contains("katex-mathml"));
            assert!(!html.contains("<math"));
        }
        let display = render_uncached(r"\frac{x^2+y^2}{z}", true).unwrap();
        assert!(display.contains("katex-display"));
        assert!(render_uncached("سلام + x", false).is_ok());
        assert!(render_uncached(r"\notacommand{", false).is_err());
    }

    #[test]
    fn cache_is_lru_bounded_and_mode_sensitive() {
        let mut cache = MathCache::new(3, 1024);
        let key = |value: &str, display| CacheKey {
            expression: value.to_owned(),
            display_mode: display,
        };
        assert!(cache.get(&key("x", false)).is_none());
        cache.insert(key("x", false), "inline".to_owned());
        assert_eq!(cache.get(&key("x", false)).as_deref(), Some("inline"));
        assert!(cache.get(&key("x", true)).is_none());
        cache.insert(key("y", false), "y".to_owned());
        cache.insert(key("z", false), "z".to_owned());
        assert_eq!(cache.get(&key("x", false)).as_deref(), Some("inline"));
        cache.insert(key("w", false), "w".to_owned());
        assert!(cache.entries.contains_key(&key("x", false)));
        assert!(!cache.entries.contains_key(&key("y", false)));
        let stats = cache.stats();
        assert!(stats.entries <= 3 && stats.bytes <= 1024 && stats.evictions == 1);
    }

    #[test]
    fn failures_are_not_cached_and_ffi_contains_panics() {
        let mut cache = MathCache::new(2, 8);
        cache.insert(
            CacheKey {
                expression: "large".into(),
                display_mode: false,
            },
            "too large".into(),
        );
        assert_eq!(cache.stats().entries, 0);
        assert!(ffi_boundary(|| panic!("contained")).is_null());
        assert!(render_uncached(r"\notacommand{", false).is_err());
    }

    #[test]
    fn concurrent_rendering_and_ffi_ownership_are_safe() {
        let threads: Vec<_> = (0..8)
            .map(|_| {
                thread::spawn(|| {
                    for _ in 0..32 {
                        let input = CString::new(r"\frac{1}{2}").unwrap();
                        let output = unsafe { potion_math_render(input.as_ptr(), 0) };
                        assert!(!output.is_null());
                        unsafe { potion_math_free(output) };
                    }
                })
            })
            .collect();
        for worker in threads {
            worker.join().unwrap();
        }
        let stats = cache().lock().unwrap().stats();
        assert!(stats.entries <= MAX_ENTRIES && stats.bytes <= MAX_BYTES && stats.hits > 0);
    }
}
