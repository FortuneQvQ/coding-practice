let input = document.getElementById("search-input");
let keyword = "";
let keywords = [];
let newsList = [];
let result = [];


// 生成新闻列表
function printNewsList(result){

    let html = "";

    // 无搜索结果
    if(result.length === 0){

        document.getElementById("news-list").innerHTML =
        `
        <div class="news-card">
            <div class="news-title">
                暂无相关资讯
            </div>
        </div>
        `;

        return;
    }

    result.forEach(news=>{

        html +=
        `
        <div class="news-card">

            <div class="news-source">
                ${highlight(news.source, keywords)}
            </div>

            <div class="news-title">
                ${highlight(news.title, keywords)}
            </div>

            <div class="news-time">
                ${highlight(news.time, keywords)}
            </div>

            <div class="news-abstract">
                ${highlight(news.abstract, keywords)}
            </div>

            <a class="detail-btn"
            href="detail/${news.id}.html?from=search&keyword=${keyword}">

                查看详情

            </a>

        </div>
        `;

    });

    document.getElementById("news-list").innerHTML = html;

}


// 正则特殊字符转义
function escapeRegExp(string){

    return string.replace(
        /[.*+?^${}()|[\]\\]/g,
        '\\$&'
    );

}


// 关键词高亮
function highlight(text, keywords){


    if(!text){

        return "";

    }

    if(keywords.length===0){

        return text;

    }

    // 长关键词优先
    let keys = [...keywords].sort((a,b)=>b.length-a.length);  //...将keywords展开，生成一个新数组

    keys.forEach(key=>{

        if(key.trim()===""){

            return;

        }

        let reg =
        new RegExp(
            escapeRegExp(key),
            "g"
        );

        text =
        text.replace(
            reg,
            match=>
            `<span class="highlight">${match}</span>`
        );

    });

    return text;

}


// 搜索函数
function search(newsList){


    let params =
    new URLSearchParams(
        window.location.search
    );

    keyword =
    input.value.trim()
    ||
    params.get("keyword")
    ||
    "";

    keywords =
    keyword
    .trim()
    .split(/\s+/)
    .filter(
        key=>key.length>0
    );


    // 空关键词显示全部
    if(keywords.length===0){

        result=[...newsList];

    }

    else{

        result =
        newsList.filter(news=>{


            return keywords.every(key=>{


                return (

                    (news.title &&
                    news.title.includes(key))


                    ||

                    (news.time &&
                    news.time.includes(key))


                    ||

                    (news.abstract &&
                    news.abstract.includes(key))


                    ||

                    (news.content &&
                    news.content.includes(key))


                    ||

                    (news.source &&
                    news.source.includes(key))


                    ||

                    (news.topic &&
                    news.topic.includes(key))


                );


            });


        });


    }



    printNewsList(result);



    let keywordBox =
    document.getElementById(
        "search-keyword"
    );


    if(keywordBox){

        keywordBox.innerHTML =
        keyword;

    }


}


// 初始化

fetch("json/news.json")


.then(response=>{


    if(!response.ok){

        throw new Error(
            "news.json加载失败"
        );

    }


    return response.json();  //把JSON文件转换成JavaScript对象


})


.then(data=>{


    newsList=data;



    // 页面第一次加载显示搜索结果

    search(newsList);





    // 搜索按钮

    let button =
    document.getElementById(
        "search-button"
    );


    if(button){


        button.addEventListener(
            "click",
            function(){

                search(newsList);

            }
        );


    }






    // 时间降序

    let sortDesc =
    document.getElementById(
        "sort-desc"
    );


    if(sortDesc){


        sortDesc.addEventListener(
            "click",
            function(){


                result.sort(
                    (a,b)=>
                    new Date(b.time)
                    -
                    new Date(a.time)
                );


                printNewsList(result);


            }
        );

    }





    // 时间升序


    let sortAsc =
    document.getElementById(
        "sort-asc"
    );


    if(sortAsc){


        sortAsc.addEventListener(
            "click",
            function(){


                result.sort(
                    (a,b)=>
                    new Date(a.time)
                    -
                    new Date(b.time)
                );


                printNewsList(result);


            }
        );


    }



})



.catch(error=>{


    console.error(
        "新闻数据加载失败:",
        error
    );


});