// Archive codec tests.
//
// The mode -> algorithm mapping here is the reverse-engineered one documented in
// docs/arch000-format.md section 9 (dispatch at binary VA 0xa68c50, selected by
// byte 7 of the archive entry header):
//
//   0x00 -> XOR        (inline at 0xa68cc5, section 4)
//   0x01 -> LZW        (0xa850f0, section 6)
//   0x02 -> PRNG stream (0xa89c90 -> 0xaa93e0, section 9)
//
// An earlier revision of this file asserted a different mapping (bogocrypt1 /
// miniz+ISAAC / bogocrypt2) and passed 18/18 while the implementation was wrong,
// because the tests were written from the implementation rather than from the
// disassembly. The LZW cases below are therefore differential: the expected bytes
// are produced by the reference decoder in tools/isaac-archive.py, not by any
// hand-written expectation.

#include "test_harness.hpp"

#include <cstring>
#include <vector>

#include "isaec/data/codec.hpp"
#include "isaec/platform/types.hpp"

using isaec::u8;
using isaec::u32;
using isaec::usize;
using isaec::data::ArchiveCodec;

namespace {

std::vector<u8> make_pattern(usize size, u32 seed) {
    std::vector<u8> data;
    data.reserve(size);
    u32 state = seed;
    for (usize i = 0; i < size; ++i) {
        state = state * 1103515245u + 12345u;
        data.push_back(static_cast<u8>((state >> 16) & 0xFF));
    }
    return data;
}

}  // namespace

#include "bogocrypt1_fixtures.inc"
#include "lzw_fixtures.inc"
#include "zip_fixture.hpp"

static const usize kBogocrypt1FixtureCount =
    sizeof(kBogocrypt1Fixtures) / sizeof(kBogocrypt1Fixtures[0]);





// --- mode 0x00: Bogocrypt1 XOR ---

ISAEC_TEST(codec_xor_is_deterministic) {
    const auto original = make_pattern(256, 7);
    std::vector<u8> a = original;
    std::vector<u8> b = original;
    CHECK(ArchiveCodec::decode(a, 0x00, 0x12345678u));
    CHECK(ArchiveCodec::decode(b, 0x00, 0x12345678u));
    CHECK(a == b);
}

ISAEC_TEST(codec_xor_changes_data) {
    std::vector<u8> data = make_pattern(256, 3);
    const std::vector<u8> original = data;
    CHECK(ArchiveCodec::decode(data, 0x00, 0x12345678u));
    CHECK(data != original);
}

ISAEC_TEST(codec_xor_depends_on_seed) {
    const auto original = make_pattern(256, 11);
    std::vector<u8> a = original;
    std::vector<u8> b = original;
    CHECK(ArchiveCodec::decode(a, 0x00, 0x00000001u));
    CHECK(ArchiveCodec::decode(b, 0x00, 0x00000002u));
    CHECK(a != b);
}

ISAEC_TEST(codec_xor_empty_input) {
    std::vector<u8> data;
    CHECK(ArchiveCodec::decode(data, 0x00, 0x12345678u));
    CHECK(data.empty());
}
// --- mode 0x00: block stride and cipher span ---

ISAEC_TEST(codec_xor_matches_reference_fixtures) {
    for (usize i = 0; i < kBogocrypt1FixtureCount; ++i) {
        const Bogocrypt1Fixture& f = kBogocrypt1Fixtures[i];
        std::vector<u8> data(f.input, f.input + f.input_size);
        REQUIRE(ArchiveCodec::decode(data, 0x00, f.special_hash, f.input_size));
        if (std::memcmp(data.data(), f.expected, f.size) != 0) {
            ::isaec::test::fail_now(std::string("bogocrypt1 fixture ") + f.name +
                                    ": decoded bytes differ from the Python reference");
        }
    }
}

ISAEC_TEST(codec_xor_cipher_span_changes_the_tail) {
    bool saw_difference = false;
    for (usize i = 0; i < kBogocrypt1FixtureCount; ++i) {
        const Bogocrypt1Fixture& f = kBogocrypt1Fixtures[i];
        if (f.size == f.input_size) continue;

        std::vector<u8> by_span(f.input, f.input + f.input_size);
        REQUIRE(ArchiveCodec::decode(by_span, 0x00, f.special_hash, f.input_size));

        std::vector<u8> by_size(f.input, f.input + f.input_size);
        REQUIRE(ArchiveCodec::decode(by_size, 0x00, f.special_hash, f.size));

        if (std::memcmp(by_span.data(), by_size.data(), f.input_size) != 0) saw_difference = true;
    }
    CHECK(saw_difference);
}

ISAEC_TEST(codec_xor_rejects_span_beyond_buffer) {
    std::vector<u8> data = make_pattern(64, 3);
    const std::vector<u8> original = data;
    CHECK(!ArchiveCodec::decode(data, 0x00, 0x12345678u, data.size() + 1));
    CHECK(data == original);
}

// Canaries past cipher_span must survive, for every tail alignment.
//
// The span sizes below deliberately cover all four residues mod 4. Only the
// residues 2 and 3 put a partial 4-byte word at the very end, and only those
// can drive an unguarded `swap(bytes, i + 3)` past the buffer; the previous
// version of this test used a single span of 37 (residue 1), which can never
// reach the canary and therefore passed even against the out-of-bounds bug.
ISAEC_TEST(codec_xor_leaves_bytes_past_span_untouched) {
    const std::vector<u8> canary(8, 0xCD);
    constexpr u32 kSpans[] = {32, 33, 34, 35, 36, 37, 38, 39, 40,
                              41, 42, 43, 44, 45, 46, 47, 63, 64, 65};
    for (u32 seed = 1; seed <= 64; ++seed) {
        for (u32 span : kSpans) {
            std::vector<u8> data = make_pattern(span, 5);
            data.insert(data.end(), canary.begin(), canary.end());

            REQUIRE(ArchiveCodec::decode(data, 0x00, seed, span));
            if (std::memcmp(data.data() + span, canary.data(), canary.size()) != 0) {
                ::isaec::test::fail_now(
                    "cipher wrote past cipher_span at seed " + std::to_string(seed) +
                    ", span " + std::to_string(span));
            }
        }
    }
}

// --- mode 0x01: LZW (differential against the Python reference decoder) ---

ISAEC_TEST(codec_lzw_matches_reference_decoder) {
    for (usize i = 0; i < kLzwFixtureCount; ++i) {
        const LzwFixture& f = kLzwFixtures[i];
        std::vector<u8> data(f.input, f.input + f.input_size);
        CHECK(ArchiveCodec::decode(data, 0x01, 0));
        if (data.size() != f.expected_size) {
            ::isaec::test::fail_now(std::string("fixture ") + f.name +
                                     ": size mismatch (got " +
                                     std::to_string(data.size()) + ", want " +
                                     std::to_string(f.expected_size) + ")");
        }
        CHECK(std::memcmp(data.data(), f.expected, f.expected_size) == 0);
    }
}

ISAEC_TEST(codec_lzw_ignores_seed) {
    // The mode-0x01 arm in the original binary performs no keystream XOR, so the
    // seed must not influence the result. This pins the corrected behaviour.
    std::vector<u8> a(kLzwFixtures[2].input,
                      kLzwFixtures[2].input + kLzwFixtures[2].input_size);
    std::vector<u8> b(a);
    CHECK(ArchiveCodec::decode(a, 0x01, 0u));
    CHECK(ArchiveCodec::decode(b, 0x01, 0xDEADBEEFu));
    CHECK(a == b);
}

ISAEC_TEST(codec_lzw_short_input_is_unchanged) {
    // Fewer than four bytes cannot hold a chunk length prefix.
    std::vector<u8> data = {0x01, 0x02};
    const std::vector<u8> original = data;
    CHECK(ArchiveCodec::decode(data, 0x01, 0));
    CHECK(data == original);
}

// --- mode 0x02: chunked DEFLATE ---

namespace {

void put_u32le(std::vector<u8>& out, u32 value) {
    out.push_back(static_cast<u8>(value & 0xFFu));
    out.push_back(static_cast<u8>((value >> 8) & 0xFFu));
    out.push_back(static_cast<u8>((value >> 16) & 0xFFu));
    out.push_back(static_cast<u8>((value >> 24) & 0xFFu));
}

// One chunk: header, then payload. `last` sets the top header bit, which every
// retail entry sets on its final chunk.
std::vector<u8> chunk(const std::vector<u8>& payload, bool last) {
    std::vector<u8> out;
    put_u32le(out, static_cast<u32>(payload.size()) | (last ? 0x80000000u : 0u));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

std::vector<u8> concat(const std::vector<u8>& a, const std::vector<u8>& b) {
    std::vector<u8> out = a;
    out.insert(out.end(), b.begin(), b.end());
    return out;
}

}  // namespace

ISAEC_TEST(codec_chunked_inflates_a_deflate_chunk) {
    const std::vector<u8> text = {'<', '?', 'x', 'm', 'l', ' ', 'v', '=', '1'};
    const std::vector<u8> stream = chunk(::isaec::test::raw_deflate(text), true);
    std::vector<u8> out;
    REQUIRE(ArchiveCodec::decode_chunked(stream.data(), stream.size(), text.size(), out));
    CHECK(out == text);
}

ISAEC_TEST(codec_chunked_copies_a_0x400_chunk_verbatim) {
    std::vector<u8> block(0x400);
    for (usize i = 0; i < block.size(); ++i) block[i] = static_cast<u8>(i * 7 + 3);
    const std::vector<u8> stream = chunk(block, true);
    std::vector<u8> out;
    REQUIRE(ArchiveCodec::decode_chunked(stream.data(), stream.size(), block.size(), out));
    CHECK(out == block);
}

ISAEC_TEST(codec_chunked_mixes_stored_and_deflate_chunks) {
    // A 0x400 stored chunk followed by a compressed tail is the shape of the
    // entries that decode from the retail archive.
    std::vector<u8> block(0x400, 0x5A);
    const std::vector<u8> tail = {0xDE, 0xAD, 0xBE, 0xEF, 0x01};
    const std::vector<u8> stream =
        concat(chunk(block, false), chunk(::isaec::test::raw_deflate(tail), true));
    std::vector<u8> out;
    REQUIRE(ArchiveCodec::decode_chunked(stream.data(), stream.size(),
                                         block.size() + tail.size(), out));
    CHECK(out.size() == block.size() + tail.size());
    CHECK(std::memcmp(out.data(), block.data(), block.size()) == 0);
    CHECK(std::memcmp(out.data() + block.size(), tail.data(), tail.size()) == 0);
}

ISAEC_TEST(codec_chunked_ignores_the_final_chunk_flag) {
    const std::vector<u8> text = {'o', 'k'};
    for (const bool last : {false, true}) {
        const std::vector<u8> stream = chunk(::isaec::test::raw_deflate(text), last);
        std::vector<u8> out;
        REQUIRE(ArchiveCodec::decode_chunked(stream.data(), stream.size(), text.size(), out));
        CHECK(out == text);
    }
}

ISAEC_TEST(codec_chunked_truncates_a_final_chunk_that_overshoots) {
    const std::vector<u8> text = {'a', 'b', 'c', 'd', 'e'};
    const std::vector<u8> stream = chunk(::isaec::test::raw_deflate(text), true);
    std::vector<u8> out;
    REQUIRE(ArchiveCodec::decode_chunked(stream.data(), stream.size(), 3, out));
    CHECK(out.size() == 3);
    CHECK(std::memcmp(out.data(), text.data(), 3) == 0);
}

ISAEC_TEST(codec_chunked_rejects_a_length_above_0x7ff) {
    std::vector<u8> stream;
    put_u32le(stream, 0x800);
    stream.resize(4 + 0x800, 0);
    std::vector<u8> out;
    CHECK(!ArchiveCodec::decode_chunked(stream.data(), stream.size(), 0x800, out));
}

ISAEC_TEST(codec_chunked_rejects_a_truncated_chunk) {
    const std::vector<u8> full = chunk(::isaec::test::raw_deflate({1, 2, 3, 4}), true);
    std::vector<u8> cut(full.begin(), full.end() - 2);
    std::vector<u8> out;
    CHECK(!ArchiveCodec::decode_chunked(cut.data(), cut.size(), 4, out));
}

ISAEC_TEST(codec_chunked_rejects_a_chunk_that_is_not_deflate) {
    const std::vector<u8> stream = chunk({0xFF, 0xFF, 0xFF, 0xFF}, true);
    std::vector<u8> out;
    CHECK(!ArchiveCodec::decode_chunked(stream.data(), stream.size(), 4, out));
}

ISAEC_TEST(codec_chunked_rejects_input_that_ends_before_the_expected_size) {
    const std::vector<u8> text = {'x', 'y'};
    const std::vector<u8> stream = chunk(::isaec::test::raw_deflate(text), true);
    std::vector<u8> out;
    CHECK(!ArchiveCodec::decode_chunked(stream.data(), stream.size(), text.size() + 1, out));
}

ISAEC_TEST(codec_chunked_empty_expected_size) {
    std::vector<u8> out = {0xFF};
    CHECK(ArchiveCodec::decode_chunked(nullptr, 0, 0, out));
    CHECK(out.empty());
}

// --- shared surface ---

ISAEC_TEST(codec_name_maps_modes) {
    CHECK(std::strcmp(ArchiveCodec::codec_name(0x00), "xor") == 0);
    CHECK(std::strcmp(ArchiveCodec::codec_name(0x01), "lzw") == 0);
    CHECK(std::strcmp(ArchiveCodec::codec_name(0x02), "chunked") == 0);
    CHECK(std::strcmp(ArchiveCodec::codec_name(0xFF), "unknown") == 0);
}

ISAEC_TEST(codec_rejects_unknown_mode) {
    std::vector<u8> data = make_pattern(32, 1);
    const std::vector<u8> original = data;
    CHECK(!ArchiveCodec::decode(data, 0x7Fu, 0));
    CHECK(data == original);
}

ISAEC_TEST(codec_mode_0x02_decode_needs_an_expected_size) {
    std::vector<u8> data = make_pattern(32, 3);
    const std::vector<u8> original = data;
    CHECK(!ArchiveCodec::decode(data, 0x02, 0));
    CHECK(data == original);
}
