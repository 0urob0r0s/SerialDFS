#!/usr/bin/env python3
"""
Minimal pytest-compatible test runner for environments without pytest.
Handles: @pytest.fixture, pytest.raises(), function-argument injection.
"""
from __future__ import annotations
import contextlib
import importlib
import importlib.util
import inspect
import sys
import types
import os

PASS = 0
FAIL = 0
ERRORS = []


class _Raises:
    def __init__(self, exc_type):
        self.exc_type = exc_type
        self.value = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        if exc_type is None:
            raise AssertionError(
                f'Expected {self.exc_type.__name__} to be raised but it was not'
            )
        if not issubclass(exc_type, self.exc_type):
            return False  # let the unexpected exception propagate
        self.value = exc_val
        return True


class _FakePytest:
    class fixture:
        def __init__(self, fn=None, **kwargs):
            if callable(fn):
                # used as @pytest.fixture with no args
                fn._is_fixture = True
                self._fn = fn
            else:
                self._fn = None

        def __call__(self, fn):
            fn._is_fixture = True
            return fn

        @staticmethod
        def __new__(cls, fn=None, **kwargs):
            if callable(fn):
                fn._is_fixture = True
                return fn
            return super().__new__(cls)

    @staticmethod
    def raises(exc_type):
        return _Raises(exc_type)

    @staticmethod
    def fail(msg=''):
        raise AssertionError(msg)


def _is_fixture(fn):
    return getattr(fn, '_is_fixture', False)


def run_module(module_name: str, module_path: str):
    global PASS, FAIL

    # Inject fake pytest before importing the test module
    fake_pytest = _FakePytest()
    # Make fixture usable as both @pytest.fixture and @pytest.fixture(...)
    fake_pytest.fixture = _make_fixture_decorator()
    sys.modules['pytest'] = fake_pytest  # type: ignore

    spec = importlib.util.spec_from_file_location(module_name, module_path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)

    # Collect fixtures
    fixtures = {}
    for name, obj in inspect.getmembers(mod):
        if callable(obj) and getattr(obj, '_is_fixture', False):
            fixtures[name] = obj

    # Collect and run tests
    tests = [(name, obj) for name, obj in inspect.getmembers(mod)
             if name.startswith('test_') and callable(obj)]

    print(f'\n=== {os.path.basename(module_path)} ({len(tests)} tests) ===')

    for test_name, test_fn in tests:
        try:
            args = _resolve_args(test_fn, fixtures, {})  # fresh cache per test
            test_fn(*args)
            print(f'  PASS  {test_name}')
            PASS += 1
        except AssertionError as e:
            msg = str(e)
            print(f'  FAIL  {test_name}' + (f': {msg}' if msg else ''))
            FAIL += 1
        except Exception as e:
            import traceback
            print(f'  ERROR {test_name}: {type(e).__name__}: {e}')
            traceback.print_exc()
            FAIL += 1


def _make_fixture_decorator():
    """Return a fixture decorator that works as @fixture or @fixture(...)."""
    def fixture(fn=None, **kwargs):
        if callable(fn):
            fn._is_fixture = True
            return fn
        # Called as @fixture(scope=...) etc — return a decorator
        def decorator(f):
            f._is_fixture = True
            return f
        return decorator
    return fixture


def _builtin_fixtures() -> dict:
    """Return dict of built-in pytest fixtures our runner supports."""
    import tempfile
    from pathlib import Path

    def tmp_path():
        d = tempfile.mkdtemp()
        return Path(d)

    return {'tmp_path': tmp_path}


def _resolve_args(fn, fixtures: dict, cache: dict) -> list:
    """Recursively resolve fixture arguments for fn."""
    all_fixtures = {**_builtin_fixtures(), **fixtures}
    sig = inspect.signature(fn)
    args = []
    for param_name in sig.parameters:
        if param_name in cache:
            args.append(cache[param_name])
        elif param_name in all_fixtures:
            fix_fn = all_fixtures[param_name]
            fix_args = _resolve_args(fix_fn, all_fixtures, cache)
            val = fix_fn(*fix_args)
            if inspect.isgenerator(val):
                val = next(val)
            cache[param_name] = val
            args.append(val)
        else:
            raise ValueError(f'No fixture for parameter {param_name!r} in {fn.__name__}')
    return args


def main():
    tests_dir = os.path.dirname(__file__)
    repo_root = os.path.abspath(os.path.join(tests_dir, '..', '..', '..', '..'))
    sys.path.insert(0, repo_root)

    modules = [
        ('test_frame',          os.path.join(tests_dir, 'test_frame.py')),
        ('test_pathjail',       os.path.join(tests_dir, 'test_pathjail.py')),
        ('test_fsops_readonly', os.path.join(tests_dir, 'test_fsops_readonly.py')),
        ('test_namemap',        os.path.join(tests_dir, 'test_namemap.py')),
        ('test_fsops_write',    os.path.join(tests_dir, 'test_fsops_write.py')),
    ]

    for mod_name, mod_path in modules:
        run_module(mod_name, mod_path)

    print(f'\nTotal: {PASS} passed, {FAIL} failed')
    sys.exit(0 if FAIL == 0 else 1)


if __name__ == '__main__':
    main()
