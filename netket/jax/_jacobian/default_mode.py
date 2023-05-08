# Copyright 2023 The NetKet Authors - All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from typing import Callable, Optional
from functools import partial
import warnings
from textwrap import dedent

import jax

import netket.jax as nkjax
from netket.utils import struct
from netket.utils.types import PyTree, Array


@struct.dataclass
class JacobianMode:
    """
    Jax-compatible string type, used to return static information from a jax-jitted
    function.
    """

    name: str = struct.field(pytree_node=False)

    def __str__(self):
        return self.name

    def __repr__(self):
        return f"JacobianMode({self.name})"

    def __hash__(self):
        return hash(self.name)

    def __eq__(self, o):
        if isinstance(o, JacobianMode):
            o = o.name
        return self.name == o


RealMode = JacobianMode("real")
ComplexMode = JacobianMode("complex")
HolomorphicMode = JacobianMode("holomorphic")


@partial(jax.jit, static_argnames=("apply_fun", "holomorphic"))
def jacobian_default_mode(
    apply_fun: Callable[[PyTree, Array], Array],
    pars: PyTree,
    model_state: Optional[PyTree],
    samples: Array,
    *,
    holomorphic: Optional[bool] = None,
) -> JacobianMode:
    """
    Returns the default `mode` for {func}`nk.jax.jacobian` given a certain
    wave-function ansatz.

    This function uses an abstract evaluation of the ansatz to determine if
    the ansatz has real or complex output, and uses that to determine the
    default mode to be used to compute the Jacobian.

    In particular:
     - for functions with a real output, it will return `RealMode`.
     - for functions with a complex output, it will return:
       - If `holomorphic==False` or it not been specified, it will return
       `ComplexMode`, which will force the calculation of both the jacobian
       and adjoint jacobian. See the documentation of{func}`nk.jax.jacobian`
       for more details.
        - If `holomorphic==True`, it will compute only the complex-valued
        jacobian, and assumes the adjoint-jacobian to be zero.

    This function will also raise an error if `holomorphic` is not specified
    but the output is complex.

    Args:
        apply_fun: A callable taking as input a pytree of parameters and the samples,
            and returning the output.
        pars: The Pytree of parameters.
        model_state: The optional `model_state`, according to the flax model definition.
        samples: An array of samples.
        holomorphic: A boolean specifying whether `apply_fun` is
            holomorphic or not (`None` by default).
    """
    homogeneous_vars = nkjax.tree_ishomogeneous(pars)
    leaf_iscomplex = nkjax.tree_leaf_iscomplex(pars)

    if holomorphic is True:
        if homogeneous_vars and leaf_iscomplex:
            ## all complex parameters
            mode = HolomorphicMode
        elif homogeneous_vars and not leaf_iscomplex:
            # all real parameters
            raise ValueError(
                dedent(
                    """
                A function with real parameters cannot be holomorphic.

                Please remove the kw-arg `holomorphic=True`.
                """
                )
            )
        else:
            # mixed complex and real parameters
            warnings.warn(
                dedent(
                    """The ansatz has non homogeneous variables, which might not behave well with the
                       holomorhic implementation.

                       Use `holomorphic=False` or mode='complex' for more accurate results but
                       lower performance.
                    """
                )
            )
            mode = HolomorphicMode
    else:
        complex_output = jax.numpy.iscomplexobj(
            jax.eval_shape(
                apply_fun,
                {"params": pars, **model_state},
                samples.reshape(-1, samples.shape[-1]),
            )
        )

        if complex_output:
            if leaf_iscomplex:
                if holomorphic is None:
                    warnings.warn(
                        dedent(
                            """
                                Complex-to-Complex model detected. Defaulting to `holomorphic=False` for
                                the calculation of its jacobian.
                                If your model is holomorphic, specify `holomorphic=True` to use a more
                                performant implementation.
                                To suppress this warning specify `holomorphic`.
                                """
                        ),
                        UserWarning,
                    )
                mode = ComplexMode
            else:
                mode = ComplexMode
        else:
            mode = RealMode
    return mode
