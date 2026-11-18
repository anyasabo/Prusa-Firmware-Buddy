#include "utils.hpp"
#include "sha256_multiuse.hpp"

#include <utility_extensions.hpp>

using bgcode::core::block_end_position;
using bgcode::core::BlockHeader;
using bgcode::core::EBlockType;
using bgcode::core::EChecksumType;
using bgcode::core::ECompressionType;
using bgcode::core::FileHeader;

namespace e2ee {

bool read_encrypted_block_header(FILE *file, BlockHeader &header, Decryptor &decryptor) {
    if (!decryptor.decrypt(file, reinterpret_cast<uint8_t *>(&header.type), sizeof(header.type))) {
        return false;
    }
    if (!decryptor.decrypt(file, reinterpret_cast<uint8_t *>(&header.compression), sizeof(header.compression))) {
        return false;
    }
    if (!decryptor.decrypt(file, reinterpret_cast<uint8_t *>(&header.uncompressed_size), sizeof(header.uncompressed_size))) {
        return false;
    }
    if (header.compression != std::to_underlying(ECompressionType::None)) {
        if (!decryptor.decrypt(file, reinterpret_cast<uint8_t *>(&header.compressed_size), sizeof(header.compressed_size))) {
            return false;
        }
    }
    return true;
}

bool is_metadata_block(EBlockType type) {
    switch (type) {
    case EBlockType::FileMetadata:
    case EBlockType::PrinterMetadata:
    case EBlockType::Thumbnail:
    case EBlockType::PrintMetadata:
    case EBlockType::SlicerMetadata:
        return true;
    default:
        return false;
    }
}

const char *BlockSequenceValidator::metadata_found(FileHeader file_header, BlockHeader block_header) {
    if (have_gcode_block || have_identity_block || have_key_block) {
        return metadata_not_beggining;
    }
    last_metadata_pos_end = block_end_position(file_header, block_header);
    return nullptr;
}

const char *BlockSequenceValidator::identity_block_found(FileHeader file_header, BlockHeader block_header) {
    if (last_metadata_pos_end != 0 && last_metadata_pos_end != block_header.get_position()) {
        return additional_data;
    }
    have_identity_block = true;
    identity_pos_end = block_end_position(file_header, block_header);
    return nullptr;
}

const char *BlockSequenceValidator::key_block_found(FileHeader file_header, BlockHeader block_header) {
    num_of_key_blocks++;
    key_block_pos_end = block_end_position(file_header, block_header);
    if (have_key_block) {
        return nullptr;
    }
    have_key_block = true;
    if (!have_identity_block) {
        return key_before_identity;
    }
    if (identity_pos_end != block_header.get_position()) {
        return additional_data;
    }
    return nullptr;
}

const char *BlockSequenceValidator::encrypted_block_found(BlockHeader block_header) {
    if (!have_identity_block) {
        return encrypted_before_identity;
    } else if (!have_key_block) {
        return encrypted_before_key;
    }
    if (key_block_pos_end != block_header.get_position()) {
        return additional_data;
    }
    return nullptr;
}

const char *BlockSequenceValidator::gcode_block_found() {
    if (have_identity_block) {
        return unencrypted_in_encrypted;
    }
    return nullptr;
}

uint32_t BlockSequenceValidator::get_num_of_key_blocks() const {
    return num_of_key_blocks;
}

void file_header_sha256(const FileHeader &file_header, SHA256MultiuseHash &hash) {
    hash.update(reinterpret_cast<const uint8_t *>(&file_header.magic), sizeof(file_header.magic));
    hash.update(reinterpret_cast<const uint8_t *>(&file_header.version), sizeof(file_header.version));
    hash.update(reinterpret_cast<const uint8_t *>(&file_header.checksum_type), sizeof(file_header.checksum_type));
}

void block_header_sha256_update(SHA256MultiuseHash &hash, const BlockHeader &header) {
    hash.update(reinterpret_cast<const uint8_t *>(&header.type), sizeof(header.type));
    hash.update(reinterpret_cast<const uint8_t *>(&header.compression), sizeof(header.compression));
    hash.update(reinterpret_cast<const uint8_t *>(&header.uncompressed_size), sizeof(header.uncompressed_size));
    if ((ECompressionType)header.compression != ECompressionType::None) {
        hash.update(reinterpret_cast<const uint8_t *>(&header.compressed_size), sizeof(header.compressed_size));
    }
}

void block_crc_sha256_update(SHA256MultiuseHash &hash, FILE *file) {
    uint32_t crc;
    if (fread(&crc, sizeof(crc), 1, file) != 1) {
        return;
    }
    hash.update(reinterpret_cast<uint8_t *>(&crc), sizeof(crc));
}

void block_sha_256_update(SHA256MultiuseHash &hash, const BlockHeader &header, EChecksumType crc, FILE *file) {
    auto file_pos = ftell(file);
    block_header_sha256_update(hash, header);
    size_t params_size = bgcode::core::block_parameters_size((EBlockType)header.type);
    uint8_t params[6]; // 6 is max params size
    size_t s = fread(params, 1, params_size, file);
    if (s != params_size) {
        return;
    }
    hash.update(params, params_size);
    static constexpr size_t BUFF_SIZE = 32;
    uint8_t buffer[BUFF_SIZE];
    size_t rest = header.compression == 0 ? header.uncompressed_size : header.compressed_size;
    while (rest > 0) {
        size_t to_read = std::min(rest, BUFF_SIZE);
        s = fread(buffer, 1, to_read, file);
        if (s != to_read) {
            return;
        }
        hash.update(buffer, to_read);
        rest -= to_read;
    }
    if (crc == EChecksumType::CRC32) {
        block_crc_sha256_update(hash, file);
    }
    fseek(file, file_pos, SEEK_SET);
}

} // namespace e2ee
