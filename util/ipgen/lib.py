# Copyright lowRISC contributors (OpenTitan project).
# Licensed under the Apache License, Version 2.0, see LICENSE for details.
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
from typing import Any, Dict, Optional, Union

import hjson  # type: ignore
from reggen.lib import (check_bool, check_int, check_keys, check_list,
                        check_name, check_str)
from reggen.params import BaseParam, Params


class TemplateParseError(Exception):
    pass


class TemplateRenderError(Exception):

    def __init__(self, message, template_vars: Any = None) -> None:
        self.message = message
        self.template_vars = template_vars

    def verbose_str(self) -> str:
        """ Get a verbose human-readable representation of the error. """

        from pprint import PrettyPrinter
        if self.template_vars is not None:
            return (self.message + "\n" + "Template variables:\n" +
                    PrettyPrinter().pformat(self.template_vars))
        return self.message


class TemplateParameter(BaseParam):
    """ A template parameter. """
    VALID_PARAM_TYPES = (
        'bool',
        'int',
        'string',
        'object',
    )

    def __init__(self, name: str, desc: Optional[str], param_type: str,
                 default: str, dtgen: object):
        assert param_type in self.VALID_PARAM_TYPES

        super().__init__(name, desc, param_type, None)
        self.default = default
        self.value = None
        self.dtgen = dtgen

    def as_dict(self) -> Dict[str, object]:
        rd = super().as_dict()
        rd['default'] = self.default
        if self.dtgen:
            rd['dtgen'] = self.dtgen
        return rd


def _parse_template_parameter(where: str, raw: object) -> TemplateParameter:
    """Check and parse the parameter in raw.

    raw must be a dictionary with specific keys. The type must be valid,
    and the hjson value itself must translate to a valid python object of
    the required type. For 'object' types we just check the value can be
    de-serialized by hjson. Perhaps this could perform a type-check instead.
    """
    rd = check_keys(raw, where, ['name', 'desc', 'type'], ['default', 'dtgen'])

    name = check_str(rd['name'], 'name field of ' + where)

    r_desc = rd.get('desc')
    if r_desc is None:
        desc = None
    else:
        desc = check_str(r_desc, 'desc field of ' + where)

    r_type = rd.get('type')
    param_type = check_str(r_type, 'type field of ' + where)
    if param_type not in TemplateParameter.VALID_PARAM_TYPES:
        raise ValueError(f'At {where}, the {name} param has an invalid type '
                         f'field {param_type!r}. Allowed values are: '
                         f'{", ".join(TemplateParameter.VALID_PARAM_TYPES)}.')

    r_default = rd.get('default')
    param_type: Union[bool, int, str, Dict[str, Any]]
    if param_type == 'bool':
        default = check_bool(
            r_default, f'default field of {name}, (a boolean parameter)')
    elif param_type == 'int':
        default = check_int(
            r_default, f'default field of {name}, (an integer parameter)')
    elif param_type == 'string':
        default = check_str(r_default, 'default field of ' + where)
    elif param_type == 'object':
        default = IpConfig._check_object(r_default,
                                         'default field of ' + where)
    else:
        assert False, f"Unknown parameter type found: {param_type!r}"

    # DT generator dictionary is not checked here, it is only forwarded to dtgen
    # but we at least check that it is a dictionary!
    dtgen = rd.get('dtgen', None)
    assert dtgen is None or isinstance(dtgen, dict), \
        f"At {where}, the 'dtgen' field must be a dictionary."
    # We add a 'doc' field if not present.
    if dtgen is not None and 'doc' not in dtgen:
        dtgen['doc'] = raw['desc']

    return TemplateParameter(name, desc, param_type, default, dtgen)


class TemplateParams(Params):
    """ A group of template parameters."""

    @classmethod
    def from_raw(cls, where: str, raw: object) -> 'TemplateParams':
        """ Produce a TemplateParams instance from an object as it is in Hjson.
        """
        ret = cls()
        rl = check_list(raw, where)
        for idx, r_param in enumerate(rl):
            entry_where = f'entry {idx + 1} in {where}'
            param = _parse_template_parameter(entry_where, r_param)
            if param.name in ret:
                raise ValueError(
                    f'At {entry_where}, found a duplicate parameter with '
                    f'name {param.name}.')
            ret.add(param)
        return ret


class IpTemplate:
    """ An IP template.

    An IP template is an IP block which needs to be parametrized before it
    can be transformed into an actual IP block (which can then be instantiated
    in a hardware design).
    """

    name: str
    params: TemplateParams
    template_path: Path

    def __init__(self, name: str, params: TemplateParams, template_path: Path):
        self.name = name
        self.params = params
        self.template_path = template_path

    @classmethod
    def from_template_path(cls, template_path: Path) -> 'IpTemplate':
        """ Create an IpTemplate from a template directory.

        An IP template directory has a well-defined structure:

        - The IP template name (TEMPLATE_NAME) is equal to the directory name.
        - It contains a file 'data/TEMPLATE_NAME.tpldesc.hjson' containing all
          configuration information related to the template.
        - It contains some files ending in '.tpl', which are Mako templates
          and are rendered into a file in the same relative location without
          the '.tpl' file extension.

        Raise an exception if checks fail for the raw template parameters.
        """

        # Check if the directory structure matches expectations.
        if not template_path.is_dir():
            raise TemplateParseError(
                f"Template path {template_path!s} is not a directory.")
        if not (template_path / 'data').is_dir():
            raise TemplateParseError(
                f"Template path {template_path!s} does not contain 'data' "
                "sub-directory.")

        # The template name equals the name of the template directory.
        template_name = template_path.stem

        # Find the template description file.
        tpldesc_file = template_path / 'data' / f'{template_name}.tpldesc.hjson'

        # Read the template description from file.
        try:
            tpldesc_obj = hjson.load(open(tpldesc_file, 'r'), use_decimal=True)
        except (OSError, FileNotFoundError) as e:
            raise TemplateParseError(
                f"Unable to read template description file {tpldesc_file!s}: "
                f"{e!s}")

        # Parse the template description file.
        where = 'template description file {!r}'.format(str(tpldesc_file))
        if 'template_param_list' not in tpldesc_obj:
            raise TemplateParseError(
                f"Required key 'template_param_list' not found in {where}")

        try:
            params = TemplateParams.from_raw(
                f"list of parameters in {where}",
                tpldesc_obj['template_param_list'])
        except ValueError as e:
            raise TemplateParseError(e) from None

        return cls(template_name, params, template_path)


class IpConfig:

    def __init__(self,
                 template_params: TemplateParams,
                 instance_name: str,
                 param_values: Dict[str, Union[str, int]] = {}):
        self.template_params = template_params
        self.instance_name = instance_name
        self.param_values = IpConfig._check_param_values(
            template_params, param_values)
        self.dtgen_params = IpConfig._extract_dtgen(template_params)

    @staticmethod
    def _check_object(obj: object, what: str) -> object:
        """Check that obj is a Hjson-serializable object.

        If not, raise a ValueError; the what argument names the object.
        """
        try:
            # Round-trip objects through the JSON encoder to get the
            # same representation no matter if we load the config from
            # file, or directly pass it on to the template. Also, catch
            # encoding/decoding errors when setting the object.
            json = hjson.dumps(obj,
                               ensure_ascii=False,
                               use_decimal=True,
                               for_json=True,
                               encoding='UTF-8')
            obj_checked = hjson.loads(json, use_decimal=True, encoding='UTF-8')
        except TypeError as e:
            raise ValueError(
                f'{what} cannot be serialized as Hjson: {e!s}') from None
        return obj_checked

    @staticmethod
    def _extract_dtgen(template_params: TemplateParams) -> Dict[str, object]:
        dtgen_params = {}
        for param_name in template_params:
            param = template_params[param_name]
            if param.dtgen:
                dtgen_params[param_name] = param.dtgen
        return dtgen_params

    @staticmethod
    def _check_param_values(template_params: TemplateParams,
                            param_values: Any) -> Dict[str, Union[str, int]]:
        """Check if parameter values are valid.

        Returns the parameter values in typed form if successful, and throws
        a ValueError otherwise.
        """
        param_values_typed = {}
        for key, value in param_values.items():
            if not isinstance(key, str):
                raise ValueError(
                    f"The IP configuration has a key {key!r} which is not a "
                    "string.")

            if key not in template_params:
                raise ValueError(
                    f"The IP configuration has a key {key!r} which is not a "
                    "valid parameter.")

            param_type = template_params[key].param_type
            if param_type not in TemplateParameter.VALID_PARAM_TYPES:
                raise ValueError(
                    f"Unknown template parameter type {param_type!r}. "
                    "Allowed types: "
                    ', '.join(TemplateParameter.VALID_PARAM_TYPES))

            if param_type == 'bool':
                param_value_typed = check_bool(
                    value, f"the key {key} of the IP configuration")
            elif param_type == 'string':
                param_value_typed = check_str(
                    value, f"the key {key} of the IP configuration")
            elif param_type == 'int':
                param_value_typed = check_int(
                    value, f"the key {key} of the IP configuration")
            elif param_type == 'object':
                param_value_typed = IpConfig._check_object(
                    value, f"the key {key} of the IP configuration")
            else:
                assert False, "Unexpected parameter type found, expand check"

            param_values_typed[key] = param_value_typed

        # Ensure that params dict is fully populated. Defaults are passed for params that
        # are not explicitly specified.
        for key, value in template_params.items():
            if key not in param_values_typed:
                param_values_typed[key] = value.default

        return param_values_typed

    @classmethod
    def from_raw(cls, template_params: TemplateParams, raw: object,
                 where: str) -> 'IpConfig':
        """ Load an IpConfig from a raw object """

        rd = check_keys(raw, 'configuration file ' + where, ['instance_name'],
                        ['param_values'])
        instance_name = check_name(rd.get('instance_name'),
                                   "the key 'instance_name' of " + where)

        if not isinstance(raw, dict):
            raise ValueError(
                "The IP configuration is expected to be a dict, but was "
                "actually a " + type(raw).__name__)

        param_values = IpConfig._check_param_values(template_params,
                                                    rd['param_values'])

        return cls(template_params, instance_name, param_values)

    @classmethod
    def from_text(cls, template_params: TemplateParams, txt: str,
                  where: str) -> 'IpConfig':
        """Load an IpConfig from an Hjson description in txt"""
        raw = hjson.loads(txt, use_decimal=True, encoding="UTF-8")
        return cls.from_raw(template_params, raw, where)

    def to_file(self, file_path: Path, header: Optional[str] = ""):
        obj = {}
        obj['instance_name'] = self.instance_name
        obj['param_values'] = self.param_values
        if self.dtgen_params:
            obj['dtgen'] = self.dtgen_params

        try:
            with open(file_path, 'w') as fp:
                if header:
                    fp.write(header)
                hjson.dump(obj,
                           fp,
                           ensure_ascii=False,
                           use_decimal=True,
                           for_json=True,
                           encoding='UTF-8',
                           indent=2)
                fp.write("\n")
        except OSError as e:
            raise TemplateRenderError(f"Cannot write to {file_path!s}: {e!s}")
