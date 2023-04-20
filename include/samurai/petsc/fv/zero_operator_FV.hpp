#pragma once
#include "../cell_based_scheme.hpp"

namespace samurai
{
    namespace petsc
    {
        template <class Field,
                  std::size_t output_field_size,
                  std::size_t dim                 = Field::dim,
                  std::size_t neighbourhood_width = 1,
                  class cfg                       = StarStencilFV<dim, output_field_size, neighbourhood_width>>
        class ZeroOperatorFV : public CellBasedScheme<cfg, Field>
        {
            using directional_bdry_config_t = typename CellBasedScheme<cfg, Field>::directional_bdry_config_t;

          public:

            using local_matrix_t = typename CellBasedScheme<cfg, Field>::local_matrix_t;

            explicit ZeroOperatorFV(Field& unknown)
                : CellBasedScheme<cfg, Field>(unknown, star_stencil<dim>(), coefficients)
            {
                this->set_name("Zero");
            }

            static std::array<local_matrix_t, cfg::scheme_stencil_size> coefficients(double)
            {
                std::array<local_matrix_t, cfg::scheme_stencil_size> coeffs;
                for (std::size_t i = 0; i < cfg::scheme_stencil_size; i++)
                {
                    coeffs[i] = zeros<local_matrix_t>();
                }
                return coeffs;
            }
        };

        template <std::size_t output_field_size, class Field>
        auto make_zero_operator_FV(Field& f)
        {
            return ZeroOperatorFV<Field, output_field_size>(f);
        }

    } // end namespace petsc
} // end namespace samurai
