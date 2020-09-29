# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: directory_metadata.proto

import sys
_b=sys.version_info[0]<3 and (lambda x:x) or (lambda x:x.encode('latin1'))
from google.protobuf.internal import enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from google.protobuf import reflection as _reflection
from google.protobuf import symbol_database as _symbol_database
# @@protoc_insertion_point(imports)

_sym_db = _symbol_database.Default()




DESCRIPTOR = _descriptor.FileDescriptor(
  name='directory_metadata.proto',
  package='',
  syntax='proto3',
  serialized_options=None,
  serialized_pb=_b('\n\x18\x64irectory_metadata.proto\"_\n\x08Metadata\x12\x1b\n\x08monorail\x18\x01 \x01(\x0b\x32\t.Monorail\x12\x12\n\nteam_email\x18\x02 \x01(\t\x12\x0f\n\x02os\x18\x03 \x01(\x0e\x32\x03.OSJ\x04\x08\r\x10\x0eR\x0bthird_party\".\n\x08Monorail\x12\x0f\n\x07project\x18\x01 \x01(\t\x12\x11\n\tcomponent\x18\x02 \x01(\t*h\n\x02OS\x12\x12\n\x0eOS_UNSPECIFIED\x10\x00\x12\t\n\x05LINUX\x10\x01\x12\x0b\n\x07WINDOWS\x10\x02\x12\x07\n\x03MAC\x10\x03\x12\x0b\n\x07\x41NDROID\x10\x04\x12\x07\n\x03IOS\x10\x05\x12\n\n\x06\x43HROME\x10\x06\x12\x0b\n\x07\x46UCHSIA\x10\x07\x62\x06proto3')
)

_OS = _descriptor.EnumDescriptor(
  name='OS',
  full_name='OS',
  filename=None,
  file=DESCRIPTOR,
  values=[
    _descriptor.EnumValueDescriptor(
      name='OS_UNSPECIFIED', index=0, number=0,
      serialized_options=None,
      type=None),
    _descriptor.EnumValueDescriptor(
      name='LINUX', index=1, number=1,
      serialized_options=None,
      type=None),
    _descriptor.EnumValueDescriptor(
      name='WINDOWS', index=2, number=2,
      serialized_options=None,
      type=None),
    _descriptor.EnumValueDescriptor(
      name='MAC', index=3, number=3,
      serialized_options=None,
      type=None),
    _descriptor.EnumValueDescriptor(
      name='ANDROID', index=4, number=4,
      serialized_options=None,
      type=None),
    _descriptor.EnumValueDescriptor(
      name='IOS', index=5, number=5,
      serialized_options=None,
      type=None),
    _descriptor.EnumValueDescriptor(
      name='CHROME', index=6, number=6,
      serialized_options=None,
      type=None),
    _descriptor.EnumValueDescriptor(
      name='FUCHSIA', index=7, number=7,
      serialized_options=None,
      type=None),
  ],
  containing_type=None,
  serialized_options=None,
  serialized_start=173,
  serialized_end=277,
)
_sym_db.RegisterEnumDescriptor(_OS)

OS = enum_type_wrapper.EnumTypeWrapper(_OS)
OS_UNSPECIFIED = 0
LINUX = 1
WINDOWS = 2
MAC = 3
ANDROID = 4
IOS = 5
CHROME = 6
FUCHSIA = 7



_METADATA = _descriptor.Descriptor(
  name='Metadata',
  full_name='Metadata',
  filename=None,
  file=DESCRIPTOR,
  containing_type=None,
  fields=[
    _descriptor.FieldDescriptor(
      name='monorail', full_name='Metadata.monorail', index=0,
      number=1, type=11, cpp_type=10, label=1,
      has_default_value=False, default_value=None,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR),
    _descriptor.FieldDescriptor(
      name='team_email', full_name='Metadata.team_email', index=1,
      number=2, type=9, cpp_type=9, label=1,
      has_default_value=False, default_value=_b("").decode('utf-8'),
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR),
    _descriptor.FieldDescriptor(
      name='os', full_name='Metadata.os', index=2,
      number=3, type=14, cpp_type=8, label=1,
      has_default_value=False, default_value=0,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR),
  ],
  extensions=[
  ],
  nested_types=[],
  enum_types=[
  ],
  serialized_options=None,
  is_extendable=False,
  syntax='proto3',
  extension_ranges=[],
  oneofs=[
  ],
  serialized_start=28,
  serialized_end=123,
)


_MONORAIL = _descriptor.Descriptor(
  name='Monorail',
  full_name='Monorail',
  filename=None,
  file=DESCRIPTOR,
  containing_type=None,
  fields=[
    _descriptor.FieldDescriptor(
      name='project', full_name='Monorail.project', index=0,
      number=1, type=9, cpp_type=9, label=1,
      has_default_value=False, default_value=_b("").decode('utf-8'),
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR),
    _descriptor.FieldDescriptor(
      name='component', full_name='Monorail.component', index=1,
      number=2, type=9, cpp_type=9, label=1,
      has_default_value=False, default_value=_b("").decode('utf-8'),
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR),
  ],
  extensions=[
  ],
  nested_types=[],
  enum_types=[
  ],
  serialized_options=None,
  is_extendable=False,
  syntax='proto3',
  extension_ranges=[],
  oneofs=[
  ],
  serialized_start=125,
  serialized_end=171,
)

_METADATA.fields_by_name['monorail'].message_type = _MONORAIL
_METADATA.fields_by_name['os'].enum_type = _OS
DESCRIPTOR.message_types_by_name['Metadata'] = _METADATA
DESCRIPTOR.message_types_by_name['Monorail'] = _MONORAIL
DESCRIPTOR.enum_types_by_name['OS'] = _OS
_sym_db.RegisterFileDescriptor(DESCRIPTOR)

Metadata = _reflection.GeneratedProtocolMessageType('Metadata', (_message.Message,), dict(
  DESCRIPTOR = _METADATA,
  __module__ = 'directory_metadata_pb2'
  # @@protoc_insertion_point(class_scope:Metadata)
  ))
_sym_db.RegisterMessage(Metadata)

Monorail = _reflection.GeneratedProtocolMessageType('Monorail', (_message.Message,), dict(
  DESCRIPTOR = _MONORAIL,
  __module__ = 'directory_metadata_pb2'
  # @@protoc_insertion_point(class_scope:Monorail)
  ))
_sym_db.RegisterMessage(Monorail)


# @@protoc_insertion_point(module_scope)