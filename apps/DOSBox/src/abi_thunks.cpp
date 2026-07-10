/* iostream seek ABI thunks.
 *
 * Zephyr's zephyr_stdint.h remaps __INT64_TYPE__ to long long, so
 * libstdc++ headers typedef std::streamoff as long long at compile
 * time — but the SDK's prebuilt libstdc++.a was compiled with
 * streamoff = long. Same LP64 register layout, different mangled
 * names. These thunks alias the long-long-mangled calls onto the
 * library's long-mangled definitions.
 */

extern "C" {

/* std::istream::seekg(streamoff, seekdir) */
void *_ZNSi5seekgElSt12_Ios_Seekdir(void *self, long off, int dir);
void *_ZNSi5seekgExSt12_Ios_Seekdir(void *self, long long off, int dir)
{
	return _ZNSi5seekgElSt12_Ios_Seekdir(self, (long)off, dir);
}

/* std::basic_streambuf<char>::seekoff(streamoff, seekdir, openmode);
 * returns fpos<mbstate_t> = { streamoff, mbstate } in x0/x1 both ways.
 */
typedef struct { long long off; long long state; } fpos16;
fpos16 _ZNSt15basic_streambufIcSt11char_traitsIcEE7seekoffElSt12_Ios_SeekdirSt13_Ios_Openmode(
	void *self, long off, int dir, int mode);
fpos16 _ZNSt15basic_streambufIcSt11char_traitsIcEE7seekoffExSt12_Ios_SeekdirSt13_Ios_Openmode(
	void *self, long long off, int dir, int mode)
{
	return _ZNSt15basic_streambufIcSt11char_traitsIcEE7seekoffElSt12_Ios_SeekdirSt13_Ios_Openmode(
		self, (long)off, dir, mode);
}

} /* extern "C" */
