use nod::read::{DiscOptions, DiscReader};
use std::cell::RefCell;
use std::ffi::{CStr, CString, c_char};
use std::io::{Read, Seek, SeekFrom};

#[derive(Default)]
struct Reader {
    disc: Option<DiscReader>,
    buffer: Vec<u8>,
    error: CString,
    size: u64,
}

thread_local! { static READER: RefCell<Reader> = RefCell::new(Reader::default()); }

#[unsafe(no_mangle)]
pub unsafe extern "C" fn nectar_disc_open(path: *const c_char) -> i32 {
    READER.with(|cell| {
        let mut reader = cell.borrow_mut();
        *reader = Reader::default();
        let result = (|| {
            let path = unsafe { CStr::from_ptr(path) }
                .to_str()
                .map_err(|e| e.to_string())?;
            let disc = DiscReader::new(path, &DiscOptions::default()).map_err(|e| e.to_string())?;
            reader.size = disc.disc_size();
            reader.disc = Some(disc);
            Ok::<_, String>(())
        })();
        if let Err(error) = result {
            reader.error = CString::new(error.replace('\0', " ")).unwrap();
            return 0;
        }
        1
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nectar_disc_read(offset: f64, size: u32) -> i32 {
    READER.with(|cell| {
        let mut reader = cell.borrow_mut();
        if !offset.is_finite()
            || offset < 0.0
            || offset.fract() != 0.0
            || size > 16 * 1024 * 1024
            || offset > reader.size as f64
            || size as f64 > reader.size as f64 - offset
        {
            reader.error = CString::new("invalid disc read range").unwrap();
            return 0;
        }
        let Reader {
            disc,
            buffer,
            error,
            ..
        } = &mut *reader;
        buffer.resize(size as usize, 0);
        let result = disc
            .as_mut()
            .ok_or_else(|| std::io::Error::other("no disc is open"))
            .and_then(|disc| {
                disc.seek(SeekFrom::Start(offset as u64))?;
                disc.read_exact(buffer)
            });
        if let Err(err) = result {
            *error = CString::new(err.to_string().replace('\0', " ")).unwrap();
            return 0;
        }
        1
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nectar_disc_size() -> f64 {
    READER.with(|r| r.borrow().size as f64)
}
#[unsafe(no_mangle)]
pub extern "C" fn nectar_disc_buffer() -> *const u8 {
    READER.with(|r| r.borrow().buffer.as_ptr())
}
#[unsafe(no_mangle)]
pub extern "C" fn nectar_disc_error() -> *const c_char {
    READER.with(|r| r.borrow().error.as_ptr())
}
#[unsafe(no_mangle)]
pub extern "C" fn nectar_disc_close() {
    READER.with(|r| *r.borrow_mut() = Reader::default());
}

fn main() {}
