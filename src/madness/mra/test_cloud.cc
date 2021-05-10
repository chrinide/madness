/*
 * test_cloud.cc
 *
 *  Created on: Aug 28, 2020
 *      Author: fbischoff
 */

#include<madness/mra/mra.h>
#include<madness/world/cloud.h>
#include<madness/mra/macrotaskq.h>
#include<madness/world/test_utilities.h>

using namespace madness;


struct gaussian {
    double a;
    gaussian() : a() {};
    gaussian(double aa) : a(aa) {}
    double operator()(const coord_4d& r) const {
        double x=r[0], y=r[1], z=r[2], aa=r[3];
        return exp(-a*(x*x + y*y + z*z * aa*aa));//*abs(sin(abs(2.0*x))) *cos(y);
    }
    double operator()(const coord_3d& r) const {
        double x=r[0], y=r[1], z=r[2];
        return exp(-a*(x*x + y*y + z*z ));//*abs(sin(abs(2.0*x))) *cos(y);
    }
};

template<typename T>
class variantT {
   std::variant<Tensor<T>> v;
};

template <typename> struct is_vector_of_world_objects: std::false_type {};
template <typename T> struct is_vector_of_world_objects<std::vector<std::is_constructible<T, World&>>> : std::true_type {};

template<typename T>
double norm(const T i1) {return fabs(i1);}

template<typename T, std::size_t NDIM>
double norm(const Function<T,NDIM>& f1) {
    return (f1).norm2();
}
template<typename T>
double norm(const Tensor<T>& t) {
    return t.normf();
}
template<typename T, std::size_t NDIM>
double norm(const std::vector<Function<T,NDIM>>& f1) {
    return norm2(f1.front().world(),f1);
}

int main(int argc, char** argv) {

    madness::World& universe = madness::initialize(argc, argv);
    startup(universe,argc,argv);

    int success=0;
    {
        Cloud cloud(universe);
//        cloud.set_debug(true);

        auto subworld_ptr = MacroTaskQ::create_worlds(universe, universe.size());
        World& subworld=*subworld_ptr;

        if (universe.rank()==0) print("entering test_cloud");
        print("my world: universe_rank, subworld_id", universe.rank(), subworld.id());

        auto dotest=[&](auto& arg) {

            test_output test_p("testing cloud/shared_ptr<Function> in world "
                        +std::to_string(subworld.id())+" "+typeid(std::get<0>(arg)).name());
            MacroTaskQ::set_pmap(subworld);

            typedef std::remove_reference_t<decltype(std::get<0>(arg))> argT;
            auto records=std::get<1>(arg);
            double universe_norm=std::get<2>(arg);

            // the first time we load from the cloud's distributed container
            auto copy_of_arg=cloud.load<argT>(subworld,records);
            double n=norm(copy_of_arg);
            double error=n-universe_norm;
            test_p.logger << "error(container)" << error << std::endl;
            if (error>1.e-10) success++;

            // the second time we load from the cloud's world-local cache
            cloud.set_force_load_from_cache(true);
            auto cached_copy_of_arg=cloud.load<argT>(subworld,records);
            double n_cached=norm(cached_copy_of_arg);
            double error_cached=n_cached-universe_norm;
            test_p.logger << "error(cache)    " << error_cached << std::endl;
            success+=test_p.end(error_cached<1.e-10 && error<1.e-10);
            cloud.set_force_load_from_cache(false);
            subworld.gop.fence();
        };
        auto tester=[&](auto&&... args) { (dotest(args), ...); };

        // test some standard objects
        real_function_3d f1 = real_factory_3d(universe).functor(gaussian(1.0));
        real_function_3d f2 = real_factory_3d(universe).functor(gaussian(2.0));
        real_function_3d f3 = real_factory_3d(universe).functor(gaussian(3.0));
        int i=3;
        long l=4l;
        Tensor<double> t(3,3);
        t.fillrandom();
        std::vector<real_function_3d> vf{f2,f3};


        auto ipair=std::make_tuple(i,cloud.store(universe, i),norm(i));
        auto lpair=std::make_tuple(l,cloud.store(universe, l),norm(l));
        auto fpair=std::make_tuple(f1,cloud.store(universe, f1),norm(f1));
        auto vpair=std::make_tuple(vf,cloud.store(universe, vf),norm(vf));
        auto tpair=std::make_tuple(t,cloud.store(universe, t),norm(t));

        tester(ipair,lpair,fpair,vpair,tpair);
        universe.gop.fence();

        MacroTaskQ::set_pmap(universe);
        universe.gop.fence();
        universe.gop.fence();

        // test pointer to FunctionImpl
        typedef std::shared_ptr<Function<double,3>::implT> impl_ptrT;
        Function<double,3> ff=real_factory_3d(universe).functor(gaussian(1.5));
        impl_ptrT p1=ff.get_impl();
        auto precords=cloud.store(universe,p1);

        {
            test_output test_ptr("testing cloud/shared_ptr<Function> in world "+std::to_string(subworld.id()));
            MacroTaskQ::set_pmap(subworld);

            auto p3=cloud.load<impl_ptrT>(subworld,precords);
            auto p4=cloud.load<impl_ptrT>(subworld,precords);
            auto p5=cloud.load<impl_ptrT>(subworld,precords);
            test_ptr.logger << "p1/p2/p3/p4" << " " << p1.get() << " " << p3.get() << " " << p4.get() << " " << p5.get() << std::endl;
            test_ptr.end(p1==p3 && p1==p4 && p1==p5
                    && p1->get_world().id()==p3->get_world().id()
                    && p1->get_world().id()==p4->get_world().id()
                    && p1->get_world().id()==p5->get_world().id());
            Function<double,3> fff;
            fff.set_impl(p3);
            Function<double,3> ffsub=real_factory_3d(subworld).functor(gaussian(1.5));
            fff-=ffsub*(1.0/universe.size());
            MacroTaskQ::set_pmap(universe);
            cloud.clear_cache(subworld);
        }
        subworld.gop.fence();
        universe.gop.fence();
        test_output test_ptr("testing cloud/shared_ptr<Function> numerics in universe");
        double ffnorm=ff.norm2();
        test_ptr.end((ffnorm<1.e-10));
        universe.gop.fence();


        // test storing tuple
        test_output test_tuple("testing tuple");
        cloud.set_debug(false);
        typedef std::tuple<double,int,Function<double,3>,impl_ptrT> tupleT;
        tupleT t1{1.0,2,f1,f2.get_impl()};
        std::vector<double> norm1{1.0,2.0,f1.norm2()};
        auto turecords=cloud.store(universe,t1);
        {
            MacroTaskQ::set_pmap(subworld);

            cloud.set_force_load_from_cache(false);
            auto t2=cloud.load<tupleT>(subworld,turecords);
            cloud.set_force_load_from_cache(true);
            auto t3=cloud.load<tupleT>(subworld,turecords);
            std::vector<double> norm2{1.0,2.0,std::get<2>(t2).norm2()};
            test_tuple.logger << "error double, int, Function " << norm1[0]-norm2[0] << "  "
                        <<  norm1[1]-norm2[1] << " " << norm1[2]-norm2[2];
            std::vector<double> norm3{1.0,2.0,std::get<2>(t3).norm2()};
            test_tuple.logger << "error double, int, Function " << norm1[0]-norm3[0] << " "
                        << norm1[1]-norm3[1] << " " << norm1[2]-norm3[2];
            double error=std::max({norm1[0]-norm2[0],norm1[1]-norm2[1],norm1[2]-norm2[2],norm1[0]-norm3[0],norm1[1]-norm3[1],norm1[2]-norm3[2]});
            double error1=std::min({norm1[0]-norm2[0],norm1[1]-norm2[1],norm1[2]-norm2[2],norm1[0]-norm3[0],norm1[1]-norm3[1],norm1[2]-norm3[2]});
            test_tuple.end(error<1.e-10 && error1>-1.e-10);
        }



        cloud.clear_cache(subworld);
    }
    universe.gop.fence();
    madness::finalize();

    return success;
}

template <> volatile std::list<detail::PendingMsg> WorldObject<WorldContainerImpl<long, std::vector<unsigned char>, madness::Hash<long> > >::pending = std::list<detail::PendingMsg>();
template <> Spinlock WorldObject<WorldContainerImpl<long, std::vector<unsigned char>, madness::Hash<long> > >::pending_mutex(0);
