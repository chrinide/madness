
/**
 \file cloud.h
 \brief Declares the \c Cloud class for storing data and transfering them between worlds
 \ingroup world

*/


#ifndef SRC_MADNESS_WORLD_CLOUD_H_
#define SRC_MADNESS_WORLD_CLOUD_H_


#include <madness/world/parallel_dc_archive.h>
#include<variant>
#include<iomanip>


/*!
  \file cloud.h
  \brief Defines and implements most of madness cloud storage

  TODO: check use of preprocessor directives
  TODO: clear cache in destructor won't work because no subworld is present -> must be explicitly called, error prone/
*/

namespace madness {

/// cloud class

/// store and load data to/from the cloud into arbitrary worlds
///
/// Distributed data is always bound to a certain world. If it needs to be
/// present in another world it can be serialized to the cloud and deserialized
/// from there again. For an example see test_cloud.cc
///
/// Data is stored into a distributed container living in the universe.
/// During storing a (replicated) list of records is returned that can be used to find the data
/// in the container. If a combined object (a vector, tuple, etc) is stored a list of records
/// will be generated. When loading the data from the world the record list will be used to
/// deserialize all stored objects.
///
/// Note that there must be a fence after the destruction of subworld containers, as in:
///
///  create subworlds
///  {
///      dcT(subworld)
///      do work
///  }
///  subworld.gop.fence();
class Cloud {

    template<typename T>
    struct Recordlist {
        std::list<T> list;

        Recordlist() : list() {};

        explicit Recordlist(const T &key) : list{key} {};

        Recordlist(const Recordlist &other) : list(other.list) {};

        Recordlist &operator+=(const Recordlist &list2) {
            for (auto &l2 : list2.list) list.push_back(l2);
            return *this;
        }

        Recordlist &operator+=(const T &key) {
            list.push_back(key);
            return *this;
        }

        T pop_front_and_return() {
            T key = list.front();
            list.pop_front();
            return key;
        }

        std::size_t size() const {
            return list.size();
        }

        friend std::ostream &operator<<(std::ostream &os, const Recordlist &arg) {
            using namespace madness::operators;
            os << arg.list;
            return os;
        }

    };

    bool debug = false;       ///< prints debug output
    bool dofence = true;      ///< fences after load/store
    bool force_load_from_cache = false;       ///< forces load from cache (mainly for debugging)

public:

    // these are all allowed types for storing
    typedef std::variant<std::size_t, int, long, double,
            std::vector<double>,
#ifdef MADNESS_TENSOR_TENSOR_H__INCLUDED
            Tensor<double>,
#endif
#ifdef MADNESS_MRA_MRA_H__INCLUDED
            Function<double, 3>,
            std::vector<Function<double, 3>>,
            std::shared_ptr<FunctionImpl<double, 3>>,
            std::vector<std::shared_ptr<FunctionImpl<double, 3>>>,
#endif
            std::monostate
    > cached_objT;

    using keyT = madness::archive::ContainerRecordOutputArchive::keyT;
    typedef std::map<keyT, cached_objT> cacheT;
    typedef Recordlist<keyT> recordlistT;

private:
    madness::WorldContainer<keyT, std::vector<unsigned char> > container;
    cacheT cached_objects;
    recordlistT local_list_of_container_keys;   // a world-local list of keys occupied in container

public:

    /// @param[in]	universe	the universe world
    Cloud(madness::World &universe) : container(universe), reading_time(0l), writing_time(0l) {
#ifndef MADNESS_TENSOR_TENSOR_H__INCLUDED
        static_assert(0,"You must #include<tensor/tensor.h> before cloud.h");
#endif
#ifndef MADNESS_MRA_MRA_H__INCLUDED
        static_assert(0,"You must #include<mra/mra.h> before cloud.h");
#endif
    }

    void set_debug(bool value) {
        debug = value;
    }

    void set_fence(bool value) {
        dofence = value;
    }

    void set_force_load_from_cache(bool value) {
        force_load_from_cache = value;
    }

    void print_timings(World &universe) const {
        double rtime = double(reading_time);
        double wtime = double(writing_time);
        universe.gop.sum(rtime);
        universe.gop.sum(wtime);
        double creads = double(cache_reads);
        double cstores = double(cache_stores);
        universe.gop.sum(creads);
        universe.gop.sum(cstores);
        if (universe.rank() == 0) {
            double precision = std::cout.precision();
            std::cout << std::fixed << std::setprecision(1);
            print("cloud storing cpu time", wtime * 0.001);
            print("cloud reading cpu time", rtime * 0.001, std::defaultfloat);
            std::cout << std::setprecision(precision) << std::scientific;
            print("cloud cache stores    ", long(cstores));
            print("cloud cache loads     ", long(creads));
        }
    }
    void clear_cache(World &subworld) {
        cached_objects.clear();
        cache_stores=0l;
        cache_reads=0l;
        local_list_of_container_keys.list.clear();
        subworld.gop.fence();
    }

    void clear_timings() {
        reading_time=0l;
        writing_time=0l;
        cache_stores=0l;
        cache_reads=0l;
        local_list_of_container_keys.list.clear();
    }


    template<typename T>
    T load(madness::World &world, const recordlistT recordlist) const {
        recordlistT rlist = recordlist;
        loadtimer t(world, reading_time);
        if constexpr (is_tuple<T>::value) {
            return load_tuple<T>(world, rlist);
        } else {
            return load_internal<T>(world, rlist);
        }
    }

    template<typename T>
    recordlistT store(madness::World &world, const T &source) {
        storetimer t(world,writing_time);
        recordlistT recordlist;
        if constexpr (is_tuple<T>::value) {
            recordlist+=store_tuple(world,source);
        } else {
            recordlist+=store_other(world,source);
        }
        if (dofence) world.gop.fence();
        return recordlist;
    }

private:

    mutable std::atomic<long> reading_time;    // in ms
    mutable std::atomic<long> writing_time;    // in ms
    mutable std::atomic<long> cache_reads;
    mutable std::atomic<long> cache_stores;

    template<typename T>
    struct is_vector : std::false_type {
    };
    template<typename T>
    struct is_vector<std::vector<T>> : std::true_type {
    };

    template<typename T>
    struct is_madness_tensor : std::false_type {
    };
    template<typename T>
    struct is_madness_tensor<Tensor<T>> : std::true_type {
    };

    template<typename>
    struct is_tuple : std::false_type {
    };
    template<typename ...T>
    struct is_tuple<std::tuple<T...>> : std::true_type {
    };

    template<typename>
    struct is_madness_function_vector : std::false_type {
    };
    template<typename T, std::size_t NDIM>
    struct is_madness_function_vector<std::vector<Function<T, NDIM>>> : std::true_type {
    };

    template<typename T>
    struct is_world_object : std::false_type {
    };
    template<typename T>
    struct is_world_object<madness::WorldObject<T>> : std::true_type {
    };

    template<typename T>
    struct is_funcimpl_ptr : std::false_type {
    };
    template<typename T, std::size_t NDIM>
    struct is_funcimpl_ptr<madness::FunctionImpl<T, NDIM> *> : std::true_type {
    };

    template<typename T>
    struct is_funcimpl_shared_ptr : std::false_type {
    };
    template<typename T, std::size_t NDIM>
    struct is_funcimpl_shared_ptr<std::shared_ptr<madness::FunctionImpl<T, NDIM>>> : std::true_type {
    };

    template<typename T> using is_world_constructible = std::is_constructible<T, World &>;

    struct storetimer {
        double cpu0;
        std::atomic<long> &wtime;
        World& world;

        storetimer(World& world, std::atomic<long> &writetime) : world(world), cpu0(cpu_time()), wtime(writetime) {}

        ~storetimer() {
            if (world.rank()==0) wtime += long((cpu_time() - cpu0) * 1000l);
        }
    };

    struct loadtimer {
        double cpu0;
        std::atomic<long> &rtime;
        World& world;

        loadtimer(World& world, std::atomic<long> &readtime) : world(world), cpu0(cpu_time()), rtime(readtime) {}

        ~loadtimer() {
            if (world.rank()==0) rtime += long((cpu_time() - cpu0) * 1000l);
        }
    };

    /// compute the record of the argument
    template<typename T>
    static keyT compute_record(const T &arg) {
        if constexpr (is_madness_function_vector<T>::value) {
            print("hurray");
        }
        if constexpr (is_madness_function<T>::value) {
//            print("hashing madness::Function");
            return hash_value(arg.get_impl()->id());
        } else if constexpr (is_madness_tensor<T>::value) {
//            print("hashing madness::Function");
            return hash_value(arg.normf());
        } else if constexpr (is_world_object<T>::value) {
//            print("hashing WorldObject");
            return hash_value(arg.id());
        } else if constexpr (is_funcimpl_ptr<T>::value) {
//            print("hashing pointer to FunctionImpl");
            return hash_value(arg->id());
        } else if constexpr (is_funcimpl_shared_ptr<T>::value) {
//            print("hashing pointer to FunctionImpl");
            return hash_value(arg->id());
        } else if constexpr (is_vector<T>::value) {
            return hash_range(arg.begin(), arg.end());
        } else {
//            print("hashing simple ", typeid(T).name());
            return keyT(hash_value(arg));
        }
    }



    template<typename T>
    void cache(madness::World &world, const T &obj, const keyT &record) const {
        const_cast<cacheT &>(cached_objects)[record] = obj;
    }

    template<typename T>
    T load_from_cache(madness::World &world, const keyT &record) const {
        if (world.rank()==0) cache_reads++;
        if (debug) print("loading", typeid(T).name(), "from cache record", record, "to world", world.id());
        if (auto obj = std::get_if<T>(&cached_objects.find(record)->second)) return *obj;
        MADNESS_EXCEPTION("failed to load from cloud-cache", 1);
        return T();
    }

    template<typename T>
    T load_internal(madness::World &world, recordlistT &recordlist) const {
        T result;
        if constexpr (is_madness_function_vector<T>::value) {
            result = load_madness_function_vector<T>(world, recordlist);
        } else {
            result = load_other<T>(world, recordlist);
        }
        return result;
    }

    bool is_cached(const keyT &key) const {
        return (cached_objects.count(key) == 1);
    }

    /// checks if a (universe) container record is used

    /// currently implemented with a local copy of the recordlist, might be
    /// reimplemented with container.find(), which would include blocking communication.
    bool is_in_container(const keyT &key) const {
        auto it = std::find(local_list_of_container_keys.list.begin(),
                            local_list_of_container_keys.list.end(), key);
        return it!=local_list_of_container_keys.list.end();
    }

    template<typename T>
    T allocator(World &world) const {
        if constexpr (is_world_constructible<T>::value) return T(world);
        return T();
    }

    template<typename T>
    recordlistT store_other(madness::World &world, const T &source) {
        auto record = compute_record(source);
        bool is_already_present= is_in_container(record);
        if (debug) {
            if (is_already_present) std::cout << "skipping ";
            std::cout << "storing object of " << typeid(T).name() << " to record " << record << std::endl;
        }

        // scope is important because of destruction ordering of world objects and fence
        if (is_already_present) {
            if (world.rank()==0) cache_stores++;
        } else {
            madness::archive::ContainerRecordOutputArchive ar(world, container, record);
            madness::archive::ParallelOutputArchive<madness::archive::ContainerRecordOutputArchive> par(world, ar);
            par & source;
            local_list_of_container_keys+=record;
        }
        if (dofence) world.gop.fence();
        return recordlistT{record};
    }

    template<typename T>
    T load_other(World &world, recordlistT &recordlist) const {
        keyT record = recordlist.pop_front_and_return();
        if (force_load_from_cache) MADNESS_CHECK(is_cached(record));

        if (is_cached(record)) return load_from_cache<T>(world, record);
        if (debug) print("loading", typeid(T).name(), "from container record", record, "to world", world.id());
        T target = allocator<T>(world);
        madness::archive::ContainerRecordInputArchive ar(world, container, record);
        madness::archive::ParallelInputArchive<madness::archive::ContainerRecordInputArchive> par(world, ar);
        par & target;
        cache(world, target, record);
        return target;
    }

    // overloaded
    template<typename T, std::size_t NDIM>
    recordlistT store_other(madness::World &world, const std::vector<Function<T, NDIM>> &source) {
        if (debug)
            std::cout << "storing " << typeid(source).name() << " of size " << source.size() << std::endl;
        recordlistT l = store_other(world, source.size());
        for (auto s : source) l += store_other(world, s);
        if (dofence) world.gop.fence();
        if (debug) std::cout << "done with vector storing; container size " << container.size() << std::endl;
        return l;
    }

    template<typename T>
    T load_madness_function_vector(World &world, recordlistT &recordlist) const {
        std::size_t sz = load_other<std::size_t>(world, recordlist);
        T target(sz);
        for (std::size_t i = 0; i < sz; ++i) {
            target[i] = load_other<typename T::value_type>(world, recordlist);
        }
        return target;
    }

    /// store a tuple in multiple records
    template<typename... Ts>
    recordlistT store_tuple(World &world, const std::tuple<Ts...> &input) {
        recordlistT v;
        auto storeaway = [&](const auto &arg) {
            v += this->store(world, arg);
        };
        auto l = [&](Ts const &... arg) {
            ((storeaway(arg)), ...);
        };
        std::apply(l, input);
        return v;
    }

    template<typename T>
    T load_tuple(madness::World &world, recordlistT &recordlist) const {
        if (debug) std::cout << "loading tuple of type " << typeid(T).name() << " to world " << world.id() << std::endl;
        T target;
        std::apply([&](auto &&... args) {
            ((args = load_internal<typename std::remove_reference<decltype(args)>::type>(world, recordlist)), ...);
        }, target);
        return target;
    }
};

} /* namespace madness */

#endif /* SRC_MADNESS_WORLD_CLOUD_H_ */
