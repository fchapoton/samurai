#pragma once

#include <mure/mure.hpp>
#include <mure/mr/mesh.hpp>
#include "criteria.hpp"




template <class Field>
bool harten(Field &u, double eps, double regularity, std::size_t ite, bool make_grad)
{

    using Config = typename Field::Config;
    using value_type = typename Field::value_type;
    constexpr auto size = Field::size;
    constexpr auto dim = Config::dim;
    constexpr auto max_refinement_level = Config::max_refinement_level;
    using interval_t = typename Config::interval_t;

    auto mesh = u.mesh();
    std::size_t min_level = mesh.min_level(), max_level = mesh.max_level();

    Field detail{"detail", mesh};

    mure::Field<Config, int, 1> tag{"tag", mesh};
    tag.array().fill(0);
    mesh.for_each_cell([&](auto &cell) {
        tag[cell] = static_cast<int>(mure::CellFlag::keep);
    });

    mure::mr_projection(u);
    u.update_bc(ite);
    mure::mr_prediction(u);
    u.update_bc(ite);


    for (std::size_t level = min_level - 1; level < max_level - ite; ++level)
    {
        auto subset = intersection(mesh[mure::MeshType::all_cells][level],
                                   mesh[mure::MeshType::cells][level + 1])
                     .on(level);
        subset.apply_op(level, compute_detail(detail, u));
    }


    for (std::size_t level = min_level; level <= max_level - ite; ++level)
    {
        int exponent = dim * (level - max_level);
        auto eps_l = std::pow(2, exponent) * eps;
        double regularity_to_use = std::min(regularity, 3.0) + dim;

        auto subset_1 = mure::intersection(mesh[mure::MeshType::cells][level],
                                           mesh[mure::MeshType::all_cells][level-1])
                       .on(level-1);


        subset_1.apply_op(level, to_coarsen_mr(detail, tag, eps_l, min_level)); // Derefinement
        subset_1.apply_op(level, to_refine_mr(detail, tag, (pow(2.0, regularity_to_use)) * eps_l, max_level)); // Refinement according to Harten
    }

    // {
    //     std::stringstream s;
    //     s << "tagify_before_enlarge_"<<ite;
    //     auto h5file = mure::Hdf5(s.str().data());
    //     h5file.add_mesh(mesh);
    //     h5file.add_field(tag);
    //     h5file.add_field(detail);
    //     h5file.add_field(u);
    // }

    for (std::size_t level = min_level; level <= max_level - ite; ++level)
    {
        auto subset_2 = intersection(mesh[mure::MeshType::cells][level],
                                     mesh[mure::MeshType::cells][level]);
        auto subset_3 = intersection(mesh[mure::MeshType::cells_and_ghosts][level],
                                     mesh[mure::MeshType::cells_and_ghosts][level]);

        subset_2.apply_op(level, mure::enlarge(tag));
        subset_2.apply_op(level, mure::keep_around_refine(tag));
        subset_3.apply_op(level, mure::tag_to_keep(tag));
    }

    // {
    //     std::stringstream s;
    //     s << "tagify_"<<ite;
    //     auto h5file = mure::Hdf5(s.str().data());
    //     h5file.add_mesh(mesh);
    //     h5file.add_field(tag);
    //     h5file.add_field(detail);
    // }

    // if (make_grad)
    {
        // COARSENING GRADUATION
        for (std::size_t level = max_level; level > 0; --level)
        {
            auto keep_subset = intersection(mesh[mure::MeshType::cells][level],
                                            mesh[mure::MeshType::all_cells][level - 1])
                            .on(level - 1);
            keep_subset.apply_op(level - 1, maximum(tag));

            xt::xtensor_fixed<int, xt::xshape<dim>> stencil;
            for (std::size_t d = 0; d < dim; ++d)
            {
                stencil.fill(0);
                for (int s = -1; s <= 1; ++s)
                {
                    if (s != 0)
                    {
                        stencil[d] = s;
                        auto subset = intersection(mesh[mure::MeshType::cells][level],
                                                translate(mesh[mure::MeshType::cells][level - 1], stencil))
                                    .on(level - 1);
                        subset.apply_op(level - 1, balance_2to1(tag, stencil));
                    }
                }
            }
        }

        // REFINEMENT GRADUATION
        for (std::size_t level = max_level; level > min_level; --level)
        {
            auto subset_1 = intersection(mesh[mure::MeshType::cells][level],
                                        mesh[mure::MeshType::cells][level]);

            subset_1.apply_op(level, extend(tag));
            
            mure::static_nested_loop<dim, -1, 2>(
                [&](auto stencil) {
                
                auto subset = intersection(translate(mesh[mure::MeshType::cells][level], stencil),
                                        mesh[mure::MeshType::cells][level-1]).on(level);

                subset.apply_op(level, make_graduation(tag));
                
            });
        }
    }

    // {
    //     std::stringstream s;
    //     s << "graduation_"<<ite;
    //     auto h5file = mure::Hdf5(s.str().data());
    //     h5file.add_mesh(mesh);
    //     h5file.add_field(tag);
    // }

    mure::CellList<Config> cell_list;
    for (std::size_t level = min_level; level <= max_level; ++level)
    {
        auto level_cell_array = mesh[mure::MeshType::cells][level];

        if (!level_cell_array.empty())
        {
            level_cell_array.for_each_interval_in_x([&](auto &index_yz, auto &interval)
            {
                for (int i = interval.start; i < interval.end; ++i)
                {
                    if (tag.array()[i + interval.index] & static_cast<int>(mure::CellFlag::refine))
                    {
                        mure::static_nested_loop<dim - 1, 0, 2>(
                            [&](auto stencil) {
                                auto index = 2 * index_yz + stencil;
                                cell_list[level + 1][index].add_point(2 * i);
                                cell_list[level + 1][index].add_point(2 * i + 1);
                            });
                    }
                    else if (tag.array()[i + interval.index] & static_cast<int>(mure::CellFlag::keep))
                    {
                        cell_list[level][index_yz].add_point(i);
                    }
                    else
                    {
                        cell_list[level-1][index_yz>>1].add_point(i>>1);
                    }
                }
            });
        }
    }
    mure::Mesh<Config> new_mesh{cell_list, mesh.initial_mesh(), min_level, max_level};

    if (new_mesh == mesh)
    {
        return true;
    }

    Field new_u{u.name(), new_mesh, u.bc()};
    new_u.array().fill(0.);

    // if (make_grad)
    {
        for (std::size_t level = min_level; level <= max_level; ++level)
        {
            auto subset = mure::intersection(mure::union_(mesh[mure::MeshType::cells][level], mesh[mure::MeshType::proj_cells][level]),
                                            new_mesh[mure::MeshType::cells][level]);
            // auto subset = mure::intersection(mesh[mure::MeshType::all_cells][level],
            //                                  new_mesh[mure::MeshType::cells][level]);
            subset.apply_op(level, copy(new_u, u));
        }
    }
    // u.update_bc(ite);

    // if (make_grad)
    {
        for (std::size_t level = min_level; level < max_level; ++level)
        {
            auto level_cell_array = mesh[mure::MeshType::cells][level];
            

            if (!level_cell_array.empty())
            {

                level_cell_array.for_each_interval_in_x([&](auto const &index_yz, auto const &interval)
                {
                    for (int i = interval.start; i < interval.end; ++i)
                    {
                        if (tag.array()[i + interval.index] & static_cast<int>(mure::CellFlag::refine))
                        {
                            mure::compute_prediction(level, interval_t{i, i + 1}, index_yz, u, new_u);
                        }
                    }
                });
            }
        }
    }

    u.mesh_ptr()->swap(new_mesh);
    std::swap(u.array(), new_u.array());

    // if (ite == 0)   {
    //     std::stringstream s;
    //     s << "new_u_by_level_"<<ite;
    //     auto h5file = mure::Hdf5(s.str().data());
    //     h5file.add_field_by_level(u.mesh(), u);

    // } 

    // {
    //     std::stringstream s;
    //     s << "new_u_"<<ite;
    //     auto h5file = mure::Hdf5(s.str().data());
    //     auto mesh = u.mesh();
    //     h5file.add_mesh(mesh);
    //     h5file.add_field(u);
    // }


    return false;
}


