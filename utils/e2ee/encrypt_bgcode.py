import sys
import os
import io
from hashlib import sha256
from zlib import crc32
from enum import IntEnum
from pathlib import Path
import struct
import hmac
import math
import argparse
from dataclasses import dataclass

from cryptography.hazmat.primitives.asymmetric import padding
from cryptography.hazmat.primitives import serialization as crypto_serialization
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.hazmat.backends import default_backend as crypto_default_backend
from cryptography.hazmat.primitives.asymmetric.types import PublicKeyTypes
from cryptography.hazmat.primitives.asymmetric.types import PrivateKeyTypes

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives import padding as aes_padding
from cryptography.hazmat.backends import default_backend

SIGN_SIZE = 256
HASH_SIZE = 32
AES_KEY_SIZE = 16
HMAC_SIZE = 32
KEY_BLOCK_SIZE = 512
CRC32_SIZE = 4


@dataclass
class EncryptKeys:
    slicer_private_key: PrivateKeyTypes
    printer_pub_key: PublicKeyTypes


@dataclass
class DecryptKeys:
    printer_private_key: PrivateKeyTypes


def aes_cbc_encrypt(message, key, iv):
    # Initialize the AES cipher in CBC mode with the provided key and IV
    cipher = Cipher(algorithms.AES(key),
                    modes.CBC(iv),
                    backend=default_backend())
    encryptor = cipher.encryptor()

    # Pad the message to be a multiple of the block size (16 bytes)
    padder = aes_padding.PKCS7(algorithms.AES.block_size).padder()
    padded_message = padder.update(message) + padder.finalize()

    # Encrypt the padded message
    encrypted_message = encryptor.update(padded_message) + encryptor.finalize()

    return encrypted_message


def aes_cbc_decrypt(encrypted_message, key, iv):
    # Initialize the AES cipher in CBC mode with the provided key and IV
    cipher = Cipher(algorithms.AES(key),
                    modes.CBC(iv),
                    backend=default_backend())
    decryptor = cipher.decryptor()

    # Decrypt the message
    padded_message = decryptor.update(encrypted_message) + decryptor.finalize()

    # Unpad the message
    unpadder = aes_padding.PKCS7(algorithms.AES.block_size).unpadder()
    message = unpadder.update(padded_message) + unpadder.finalize()

    return message


class ChecksumType(IntEnum):
    NONE = 0
    CRC32 = 1


class BlockType(IntEnum):
    FileMetadata = 0
    Gcode = 1
    SlicerMetadata = 2
    PrinterMetadata = 3
    PrintMetadata = 4
    Thumbnail = 5
    IdentityBlock = 6
    KeyBlock = 7


class AsymAlgo(IntEnum):
    No = 0
    RSA = 1


class SymAlgo(IntEnum):
    AES_128_CBC = 0


class Compression(IntEnum):
    No = 0
    Deflate = 1
    HeatShrink_11_4 = 2
    HeatShrink_12_4 = 3


class FileHeader:

    def __init__(self, file) -> None:
        self.magic = list(struct.unpack("cccc", file.read(4)))
        self.version = struct.unpack("I", bytearray(file.read(4)))[0]
        self.checksumType = struct.unpack("h", bytearray(file.read(2)))[0]
        #print(', '.join("%s: %s" % item for item in vars(self).items()))

    def bytes(self) -> bytes:
        buffer = bytearray()
        buffer.extend(struct.pack("<%uc" % len(self.magic), *self.magic))
        buffer.extend(struct.pack("<I", self.version))
        buffer.extend(struct.pack("<h", self.checksumType))
        return buffer

    def check(self):
        assert (self.magic == [b"G", b"C", b"D", b"E"])
        assert (self.version == 1)
        assert (self.checksumType in [0, 1])


class BlockHeader:

    def __init__(self, header_type, compression, uncompressed_size,
                 compressed_size) -> None:
        self.type = header_type
        self.compression = compression
        self.uncompressed_size = uncompressed_size
        self.compressed_size = compressed_size
        #print(', '.join("%s: %s" % item for item in vars(self).items()))

    def bytes(self) -> bytes:
        buffer = bytearray()
        buffer.extend(struct.pack("<h", self.type))
        buffer.extend(struct.pack("<h", self.compression))
        buffer.extend(struct.pack("<I", self.uncompressed_size))
        if self.compression != 0:
            buffer.extend(struct.pack("<I", self.compressed_size))
            assert (len(buffer) == 12)
        else:
            assert (len(buffer) == 8)
        return buffer

    def size(self) -> int:
        return 8 if self.compression == 0 else 12

    def payloadSizeWithParams(self):
        if self.type == BlockType.Thumbnail:
            additional = 6
        #TODO pramaeters to these blocks
        elif self.type == BlockType.IdentityBlock or self.type == BlockType.KeyBlock:
            additional = 0
        else:
            additional = 2
        return additional + self.compressed_size


def readBlockHeader(file) -> BlockHeader:
    header_type = struct.unpack("<h", bytearray(file.read(2)))[0]
    compression = struct.unpack("<h", bytearray(file.read(2)))[0]
    uncompressed_size = struct.unpack("<I", bytearray(file.read(4)))[0]
    if compression == 0:
        compressed_size = uncompressed_size
    else:
        compressed_size = struct.unpack("<I", bytearray(file.read(4)))[0]
    return BlockHeader(header_type, compression, uncompressed_size,
                       compressed_size)


class IdentityBlock:

    def __init__(self) -> None:
        self.slicer_public_key = bytes()
        self.identity_name = bytes()
        #TODO
        self.asym_algo = AsymAlgo.RSA
        self.sym_algo = SymAlgo.AES_128_CBC
        self.intro_hash = bytes()
        self.key_bloks_hash = bytes()
        self.total_file_size = 0
        self.header = BlockHeader(BlockType.IdentityBlock, Compression.No,
                                  self.dataSize(), self.dataSize())
        self.sign = bytes()

    def init(self,
             slicer_pub_key,
             name,
             intro_hash,
             key_block_hash,
             total_file_size=0):
        self.slicer_public_key = slicer_pub_key
        self.identity_name = name
        #TODO
        self.algorithms_used = None
        self.intro_hash = intro_hash
        self.key_bloks_hash = key_block_hash
        #set later
        self.total_file_size = total_file_size
        self.header = BlockHeader(BlockType.IdentityBlock, Compression.No,
                                  self.dataSize(), self.dataSize())
        #print(', '.join("%s: %s" % item for item in vars(self).items()))

    def read(self, block: bytes):
        stream = io.BytesIO(block)
        slicer_pub_key_len = struct.unpack("<h", stream.read(2))[0]
        slicer_pub_key = stream.read(slicer_pub_key_len)
        identity_name_len = struct.unpack("<B", bytearray(stream.read(1)))[0]
        identity_name = stream.read(identity_name_len)
        self.asym_algo = int.from_bytes(stream.read(1), 'little')
        if self.asym_algo != AsymAlgo.RSA and self.asym_algo != AsymAlgo.No:
            print("Unsupported asymetric crypto algorithm")
            sys.exit()
        self.sym_algo = int.from_bytes(stream.read(1), 'little')
        if self.sym_algo != SymAlgo.AES_128_CBC:
            print("Unsupported symetric crypto algorithm")
            sys.exit()
        intro_hash = bytearray(stream.read(32))
        key_blocks_hash = bytearray(stream.read(32))
        total_file_size = struct.unpack("<q", bytearray(stream.read(8)))[0]
        self.sign = stream.read(SIGN_SIZE)
        self.init(slicer_pub_key, identity_name, intro_hash, key_blocks_hash,
                  total_file_size)

    def bytesToSign(self) -> bytes:
        buffer = bytearray()
        buffer.extend(struct.pack("<h", len(self.slicer_public_key)))
        buffer.extend(self.slicer_public_key)
        buffer.extend(struct.pack("<B", len(self.identity_name)))
        buffer.extend(self.identity_name)
        buffer.extend(self.asym_algo.to_bytes(1, "little"))
        buffer.extend(self.sym_algo.to_bytes(1, "little"))
        #TODO algorithms
        buffer.extend(self.intro_hash)
        buffer.extend(self.key_bloks_hash)
        buffer.extend(struct.pack("<q", self.total_file_size))
        return buffer

    def signature(self, slicer_private_key: PrivateKeyTypes) -> bytes:
        return sign_message(slicer_private_key, self.bytesToSign())

    def verify(self) -> None:
        assert (len(self.sign) == SIGN_SIZE)
        key = crypto_serialization.load_der_public_key(self.slicer_public_key,
                                                       crypto_default_backend)
        verify(key, self.bytesToSign(), self.sign)

    def bytes(self, checksumType: ChecksumType,
              slicer_private_key: PrivateKeyTypes) -> bytes:
        buffer = bytearray()
        buffer.extend(self.header.bytes())
        buffer.extend(self.bytesToSign())
        buffer.extend(self.signature(slicer_private_key))
        if checksumType == ChecksumType.CRC32:
            buffer.extend(crc32(buffer).to_bytes(4, 'little'))
        return buffer

    def dataSize(self) -> int:
        size = (
            2 + len(self.slicer_public_key) + 1 + len(self.identity_name) +
            2  #algos
            #TODO algorithms
            + 32 + 32 + 8) + SIGN_SIZE
        return size

    def fullSize(self, checksumType: ChecksumType) -> int:
        size = self.header.size() + self.dataSize()
        if checksumType == ChecksumType.CRC32:
            size += CRC32_SIZE

        return size


class GcodeBlock:

    def __init__(self, block_header: BlockHeader, data: bytes,
                 include_crc: bool) -> None:
        self.header = block_header
        self.params = data[:2]
        self.gcode = data[2:]
        self.include_crc = include_crc

    #def fullSize(self) -> int:
    #    return self.header.size() + len(self.params) + len(self.gcode)

    def encryptedSize(self) -> int:
        if len(self.gcode) % 16 == 0:
            encrypted_gcode_size = len(self.gcode) + 16
        else:
            encrypted_gcode_size = int(16 * math.ceil(len(self.gcode) / 16.))
        size = self.header.size() + len(
            self.params) + encrypted_gcode_size + HMAC_SIZE
        if self.include_crc:
            size += CRC32_SIZE
        return size


def emptyIdentityBlock() -> IdentityBlock:
    return IdentityBlock()


def encrypt(key, message):
    return key.encrypt(
        message,
        padding.OAEP(mgf=padding.MGF1(algorithm=hashes.SHA256()),
                     algorithm=hashes.SHA256(),
                     label=None))


def decrypt(key, message):
    return key.decrypt(
        message,
        padding.OAEP(mgf=padding.MGF1(algorithm=hashes.SHA256()),
                     algorithm=hashes.SHA256(),
                     label=None))


def sign_message(key, message):
    return key.sign(
        message,
        padding.PSS(mgf=padding.MGF1(hashes.SHA256()),
                    salt_length=padding.PSS.DIGEST_LENGTH), hashes.SHA256())


def verify(key, message, signature):
    key.verify(
        signature, message,
        padding.PSS(mgf=padding.MGF1(hashes.SHA256()),
                    salt_length=padding.PSS.DIGEST_LENGTH), hashes.SHA256())


def key_block_encrypt(keys: EncryptKeys, key_block):
    slicer_pub_key_hash = sha256(
        keys.slicer_private_key.public_key().public_bytes(
            crypto_serialization.Encoding.DER,
            crypto_serialization.PublicFormat.SubjectPublicKeyInfo))
    printer_pub_key_hash = sha256(
        keys.printer_pub_key.public_bytes(
            crypto_serialization.Encoding.DER,
            crypto_serialization.PublicFormat.SubjectPublicKeyInfo))
    inner = slicer_pub_key_hash.digest() + printer_pub_key_hash.digest(
    ) + key_block
    #this must be a key not bytes
    outer = encrypt(keys.printer_pub_key, inner)
    signature = sign_message(keys.slicer_private_key, outer)

    return outer + signature


def key_block_decrypt(slicer_pub_key, printer_pub_key, printer_private_key,
                      enc_key_block):
    sig = enc_key_block[len(enc_key_block) - SIGN_SIZE:]
    outer_msg = enc_key_block[:len(enc_key_block) - SIGN_SIZE]
    verify(slicer_pub_key, outer_msg, sig)

    inner_msg = decrypt(printer_private_key, outer_msg)
    slicer_pub_key_hash_dec = inner_msg[:HASH_SIZE]
    printer_pub_key_hash_dec = inner_msg[HASH_SIZE:2 * HASH_SIZE]
    slicer_pub_key_hash = sha256(
        slicer_pub_key.public_bytes(
            crypto_serialization.Encoding.DER,
            crypto_serialization.PublicFormat.SubjectPublicKeyInfo))
    printer_pub_key_hash = sha256(
        printer_pub_key.public_bytes(
            crypto_serialization.Encoding.DER,
            crypto_serialization.PublicFormat.SubjectPublicKeyInfo))
    key_block = inner_msg[2 * HASH_SIZE:]
    assert (slicer_pub_key_hash.digest() == slicer_pub_key_hash_dec)
    assert (printer_pub_key_hash.digest() == printer_pub_key_hash_dec)
    return key_block


def readHashWriteMetadata(file_header, in_file, out_file) -> tuple[bytes, int]:
    metadata_buffer = bytearray(file_header.bytes())
    while True:
        block_header = readBlockHeader(in_file)
        if block_header.type == 1:
            in_file.seek(-block_header.size(), 1)
            break

        size = block_header.payloadSizeWithParams()
        block = in_file.read(size)
        metadata_buffer.extend(block_header.bytes())
        metadata_buffer.extend(block)
        if file_header.checksumType == ChecksumType.CRC32:
            metadata_buffer.extend(in_file.read(4))

    out_file.write(metadata_buffer)
    return sha256(metadata_buffer).digest(), len(metadata_buffer)


def readGcodeBlocks(in_file: io.BufferedReader,
                    checksumType: ChecksumType) -> tuple[list, int]:
    gcode_blocks = []
    gcode_blocks_encpryted_size = 0
    while True:
        block_header = readBlockHeader(in_file)
        size = block_header.payloadSizeWithParams()
        curr_gcode_block = GcodeBlock(block_header, in_file.read(size),
                                      checksumType == ChecksumType.CRC32)
        gcode_blocks.append(curr_gcode_block)
        gcode_blocks_encpryted_size += curr_gcode_block.encryptedSize()
        if checksumType == ChecksumType.CRC32:
            _ = in_file.read(
                4)  #Read and ignore the CRC, we will make a new one

        if in_file.tell() == os.fstat(in_file.fileno()).st_size:
            break
    return gcode_blocks, gcode_blocks_encpryted_size


class KeyBlock:

    def __init__(self, rsa_keys: EncryptKeys) -> None:
        enc_aes_key = os.urandom(16)
        sign_key = os.urandom(16)
        self.plain_key_block = enc_aes_key + sign_key
        self.enc_key_block = key_block_encrypt(rsa_keys, self.plain_key_block)
        #hash the encrypted data or plain??
        self.header = BlockHeader(BlockType.KeyBlock, Compression.No,
                                  len(self.enc_key_block),
                                  len(self.enc_key_block))

    def encAesKey(self) -> bytes:
        return self.plain_key_block[:16]

    def signKey(self) -> bytes:
        return self.plain_key_block[16:]

    def hash(self) -> bytes:
        return sha256(self.plain_key_block).digest()

    def size(self, checksumType: ChecksumType) -> int:
        size = self.header.size() + len(self.enc_key_block)
        if checksumType == ChecksumType.CRC32:
            size += CRC32_SIZE

        return size

    def bytes(self, checksumType: ChecksumType) -> bytes:
        buffer = bytearray()
        buffer.extend(self.header.bytes())
        buffer.extend(self.enc_key_block)
        if checksumType == ChecksumType.CRC32:
            buffer.extend(crc32(buffer).to_bytes(4, 'little'))

        return buffer


def generateIdentityBlock(key_block: KeyBlock, checksumType: ChecksumType,
                          slicer_private_key: PrivateKeyTypes,
                          gcode_blocks_encrypted_size, metadata_size,
                          metadata_hash) -> IdentityBlock:
    slicer_public_key_bytes = slicer_private_key.public_key().public_bytes(
        crypto_serialization.Encoding.DER,
        crypto_serialization.PublicFormat.SubjectPublicKeyInfo)
    identity_block = IdentityBlock()
    identity_block.init(slicer_public_key_bytes, b"Cool name", metadata_hash,
                        key_block.hash())
    identity_block.total_file_size = metadata_size + key_block.size(
        checksumType) + identity_block.fullSize(
            checksumType) + gcode_blocks_encrypted_size
    print(identity_block.total_file_size)

    return identity_block


def encryptAndWriteGcodeBlocks(file: io.BufferedWriter, gcode_blocks: list,
                               sign_key: bytes, enc_aes_key: bytes) -> None:
    for gcode_block in gcode_blocks:
        #Should this suppose to be the before or after the block header??
        iv = (file.tell() + gcode_block.header.size()).to_bytes(16, 'little')

        encrypted_block = aes_cbc_encrypt(gcode_block.gcode, enc_aes_key, iv)
        gcode_block.header.compressed_size = len(encrypted_block) + HMAC_SIZE
        hmac_data = gcode_block.header.bytes() + iv + encrypted_block
        hmac_sig = hmac.new(sign_key, hmac_data, sha256).digest()
        file.write(gcode_block.header.bytes())
        file.write(gcode_block.params)
        file.write(encrypted_block)
        file.write(hmac_sig)
        file.write(
            crc32(gcode_block.header.bytes() + encrypted_block +
                  hmac_sig).to_bytes(4, 'little'))


def encrypt_bgcode(in_filename, out_filename, rsa_keys: EncryptKeys):
    print("Reading bgcode from:", in_filename)
    in_file = open(in_filename, mode='rb')
    print("Writing encrypted bgcode to:", out_filename)
    out_file = open(out_filename, 'wb')

    file_header = FileHeader(in_file)
    file_header.check()

    metadata_hash, metadata_size = readHashWriteMetadata(
        file_header, in_file, out_file)

    gcode_blocks, gcode_blocks_encrypted_size = readGcodeBlocks(
        in_file, file_header.checksumType)

    key_block = KeyBlock(rsa_keys)
    identity_block = generateIdentityBlock(key_block, file_header.checksumType,
                                           rsa_keys.slicer_private_key,
                                           gcode_blocks_encrypted_size,
                                           metadata_size, metadata_hash)

    out_file.write(
        identity_block.bytes(file_header.checksumType,
                             rsa_keys.slicer_private_key))
    out_file.write(key_block.bytes(file_header.checksumType))

    encryptAndWriteGcodeBlocks(out_file, gcode_blocks, key_block.signKey(),
                               key_block.encAesKey())


def is_metadata_block(type) -> bool:
    match type:
        case BlockType.FileMetadata:
            return True
        case BlockType.PrinterMetadata:
            return True
        case BlockType.Thumbnail:
            return True
        case BlockType.PrintMetadata:
            return True
        case BlockType.SlicerMetadata:
            return True
        case _:
            return False


def decrypt_gcode_block(aes_enc_key, sign_key, block_header_bytes, iv,
                        block) -> bytes:
    enc_gcode = block[:len(block) - HMAC_SIZE]
    file_hmac = block[len(block) - HMAC_SIZE:]

    hmac_data = block_header_bytes + iv + enc_gcode
    computed_hmac = hmac.new(sign_key, hmac_data, sha256)
    if not hmac.compare_digest(file_hmac, computed_hmac.digest()):
        print("HMAC mismatch")
        sys.exit()

    return aes_cbc_decrypt(enc_gcode, aes_enc_key, iv)


def decrypt_bgcode(in_filename, out_filename, keys: DecryptKeys):
    enc_file = open(in_filename, 'rb')
    file_header = FileHeader(enc_file)
    file_header.check()
    out_bytes = bytearray()
    out_bytes.extend(file_header.bytes())
    #TODO ensure these are assigned from key/identity block before using them
    gcode_enc_key = bytes()
    gcode_sign_key = bytes()
    crc = bytes()
    #TODO
    identity_block = IdentityBlock()
    slicer_pub_key = None

    while True:
        block_header = readBlockHeader(enc_file)
        iv = enc_file.tell().to_bytes(16, byteorder='little')
        size = block_header.payloadSizeWithParams()
        block = enc_file.read(size)
        if file_header.checksumType == ChecksumType.CRC32:
            crc = enc_file.read(4)

        if is_metadata_block(block_header.type):
            out_bytes.extend(block_header.bytes())
            out_bytes.extend(block)
            if file_header.checksumType == ChecksumType.CRC32:
                out_bytes.extend(crc)
        match block_header.type:
            case BlockType.IdentityBlock:
                identity_block.read(block)
                metadata_hash = sha256(out_bytes).digest()
                assert (metadata_hash == identity_block.intro_hash)
                print("Identity name: ", identity_block.identity_name)
                identity_block.verify()
                slicer_pub_key = crypto_serialization.load_der_public_key(
                    identity_block.slicer_public_key, crypto_default_backend)

            case BlockType.KeyBlock:
                if identity_block.asym_algo == AsymAlgo.RSA:
                    key_block = key_block_decrypt(
                        slicer_pub_key, keys.printer_private_key.public_key(),
                        keys.printer_private_key, block)
                elif identity_block.asym_algo == AsymAlgo.No:
                    key_block = block
                else:
                    print("Unsupported asymetric crypto algorithm")
                    sys.exit()

                assert (identity_block.key_bloks_hash == sha256(
                    key_block).digest())
                gcode_enc_key = key_block[:AES_KEY_SIZE]
                gcode_sign_key = key_block[AES_KEY_SIZE:]

            case BlockType.Gcode:
                if identity_block.sym_algo == SymAlgo.AES_128_CBC:
                    decoded_block = decrypt_gcode_block(
                        gcode_enc_key, gcode_sign_key, block_header.bytes(),
                        iv, block[2:])
                else:
                    print("Unsupported symetric crypto algorithm")
                    sys.exit()

                block_header.compressed_size = len(decoded_block)
                out_bytes.extend(block_header.bytes())
                out_bytes.extend(block[:2])
                out_bytes.extend(decoded_block)
                if file_header.checksumType == ChecksumType.CRC32:
                    out_bytes.extend(
                        crc32(block_header.bytes() + block[:2] +
                              decoded_block).to_bytes(4, 'little'))

        if enc_file.tell() == os.fstat(enc_file.fileno()).st_size:
            break
    out_file = open(out_filename, 'wb')
    out_file.write(out_bytes)


def savePrivateKey(private_key, filename) -> None:
    private_key_file = open(filename, 'wb')
    private_key_file.write(
        private_key.private_bytes(
            encoding=crypto_serialization.Encoding.DER,
            format=crypto_serialization.PrivateFormat.TraditionalOpenSSL,
            encryption_algorithm=crypto_serialization.NoEncryption()))


def savePublicKey(public_key, filename) -> None:
    public_key_file = open("printer_public_key.der", 'wb')
    public_key_file.write(
        public_key.public_bytes(
            crypto_serialization.Encoding.DER,
            crypto_serialization.PublicFormat.SubjectPublicKeyInfo))


def generateKeys():
    printer_private_key = rsa.generate_private_key(
        backend=crypto_default_backend(), public_exponent=65537, key_size=2048)
    savePrivateKey(printer_private_key, "printer_private_key.der")

    printer_public_key = printer_private_key.public_key()
    savePublicKey(printer_public_key, "printer_public_key.der")

    slicer_private_key = rsa.generate_private_key(
        backend=crypto_default_backend(), public_exponent=65537, key_size=2048)
    savePrivateKey(slicer_private_key, "slicer_private_key.der")


def main():
    parser = argparse.ArgumentParser(
        prog="Bgcode encrypt/decrypt",
        description="Encrypts/decrypts bgcodes for e2ee",
        epilog="TADA")

    parser.add_argument("-e",
                        "--encrypt",
                        action=argparse.BooleanOptionalAction)
    parser.add_argument("-d",
                        "--decrypt",
                        action=argparse.BooleanOptionalAction)
    parser.add_argument("-g",
                        "--generate-keys",
                        action=argparse.BooleanOptionalAction)
    parser.add_argument("-in", "--input-file", type=Path)
    parser.add_argument("-out", "--output-file", type=Path)
    parser.add_argument("-spk", "--slicer-private-key", type=Path)
    parser.add_argument("-ppk", "--printer-private-key", type=Path)
    parser.add_argument("-ppubk", "--printer-public-key", type=Path)

    args = parser.parse_args()
    if args.encrypt and args.decrypt:
        parser.error("Choose only one of --encrypt(-e), --decypt(-d)")

    if args.generate_keys:
        generateKeys()

    if args.encrypt and (not args.slicer_private_key
                         or not args.printer_public_key or not args.input_file
                         or not args.output_file):
        parser.error("Invalid arguments")
    if args.decrypt and (not args.printer_private_key or not args.input_file
                         or not args.output_file):
        parser.error("Invalid arguments")

    if args.encrypt:
        with open(args.slicer_private_key, "br") as key_file:
            slicer_private_key = crypto_serialization.load_der_private_key(
                key_file.read(), None)
        with open(args.printer_public_key, "br") as key_file:
            printer_public_key = crypto_serialization.load_der_public_key(
                key_file.read())

        encrypt_bgcode(args.input_file, args.output_file,
                       EncryptKeys(slicer_private_key, printer_public_key))

    elif args.decrypt:
        with open(args.printer_private_key, "br") as key_file:
            printer_private_key = crypto_serialization.load_der_private_key(
                key_file.read(), None)
        with open(args.printer_public_key, "br") as key_file:
            printer_public_key = crypto_serialization.load_der_public_key(
                key_file.read())

        decrypt_bgcode(args.input_file, args.output_file,
                       DecryptKeys(printer_private_key))

    if not args.encrypt and not args.decrypt and not args.generate_keys:
        parser.error("Nothing to do")


main()
